/**
 * CYW55500 WiFi - SDIO Driver Implementation
 * Bare-metal driver for RISC-V SoC
 */

#include "baremetal.h"
#include "cyw55500_sdio.h"

/*============================================================================
 * Private Data
 *============================================================================*/

static cyw_dev_t g_cyw_dev;

/*============================================================================
 * Debug Macros (implement as needed)
 *============================================================================*/

#ifndef CYW_DEBUG
#define CYW_DEBUG 1
#endif

#if CYW_DEBUG
extern int printf(const char *fmt, ...);
#define DBG(fmt, ...)   printf("[CYW] " fmt "\n", ##__VA_ARGS__)
#define ERR(fmt, ...)   printf("[CYW ERR] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG(fmt, ...)
#define ERR(fmt, ...)
#endif

/*============================================================================
 * Helper Functions
 *============================================================================*/

static inline void delay_us(uint32_t us)
{
    if (g_cyw_dev.ops && g_cyw_dev.ops->delay_us) {
        g_cyw_dev.ops->delay_us(us);
    }
}

static inline void delay_ms(uint32_t ms)
{
    if (g_cyw_dev.ops && g_cyw_dev.ops->delay_ms) {
        g_cyw_dev.ops->delay_ms(ms);
    }
}

/*============================================================================
 * SDIO Low-level Access
 *============================================================================*/

cyw_err_t cyw_sdio_read8(uint8_t func, uint32_t addr, uint8_t *val)
{
    if (!g_cyw_dev.ops || !g_cyw_dev.ops->cmd52_read) {
        return CYW_ERR_INVALID;
    }
    int ret = g_cyw_dev.ops->cmd52_read(func, addr, val);
    return (ret == 0) ? CYW_OK : CYW_ERR_IO;
}

cyw_err_t cyw_sdio_write8(uint8_t func, uint32_t addr, uint8_t val)
{
    if (!g_cyw_dev.ops || !g_cyw_dev.ops->cmd52_write) {
        return CYW_ERR_INVALID;
    }
    int ret = g_cyw_dev.ops->cmd52_write(func, addr, val);
    return (ret == 0) ? CYW_OK : CYW_ERR_IO;
}

static cyw_err_t sdio_read_bytes(uint8_t func, uint32_t addr,
                                  uint8_t *data, uint32_t len, bool incr)
{
    if (!g_cyw_dev.ops || !g_cyw_dev.ops->cmd53_read) {
        return CYW_ERR_INVALID;
    }
    int ret = g_cyw_dev.ops->cmd53_read(func, addr, data, len, incr);
    return (ret == 0) ? CYW_OK : CYW_ERR_IO;
}

static cyw_err_t sdio_write_bytes(uint8_t func, uint32_t addr,
                                   const uint8_t *data, uint32_t len, bool incr)
{
    if (!g_cyw_dev.ops || !g_cyw_dev.ops->cmd53_write) {
        return CYW_ERR_INVALID;
    }
    int ret = g_cyw_dev.ops->cmd53_write(func, addr, data, len, incr);
    return (ret == 0) ? CYW_OK : CYW_ERR_IO;
}

/*============================================================================
 * Backplane Window Management
 *============================================================================*/

static cyw_err_t set_backplane_window(uint32_t addr)
{
    cyw_dev_t *dev = &g_cyw_dev;
    uint32_t window = addr & SBSDIO_SBWINDOW_MASK;
    cyw_err_t err;

    /* Check if window already set */
    if (dev->sbwad_valid && dev->sbwad == window) {
        return CYW_OK;
    }

    /* Set window address bytes */
    err = cyw_sdio_write8(SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRLOW,
                          (window >> 8) & 0xFF);
    if (err != CYW_OK) return err;

    err = cyw_sdio_write8(SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRMID,
                          (window >> 16) & 0xFF);
    if (err != CYW_OK) return err;

    err = cyw_sdio_write8(SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRHIGH,
                          (window >> 24) & 0xFF);
    if (err != CYW_OK) return err;

    dev->sbwad = window;
    dev->sbwad_valid = true;

    return CYW_OK;
}

/*============================================================================
 * Backplane Read/Write
 *============================================================================*/

cyw_err_t cyw_sdio_read32(uint32_t addr, uint32_t *val)
{
    cyw_err_t err;
    uint32_t offset;

    err = set_backplane_window(addr);
    if (err != CYW_OK) return err;

    offset = (addr & SBSDIO_SB_OFT_ADDR_MASK) | SBSDIO_SB_ACCESS_2_4B_FLAG;

    uint8_t data[4];
    err = sdio_read_bytes(SDIO_FUNC_1, offset, data, 4, true);
    if (err != CYW_OK) return err;

    *val = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    return CYW_OK;
}

cyw_err_t cyw_sdio_write32(uint32_t addr, uint32_t val)
{
    cyw_err_t err;
    uint32_t offset;

    err = set_backplane_window(addr);
    if (err != CYW_OK) return err;

    offset = (addr & SBSDIO_SB_OFT_ADDR_MASK) | SBSDIO_SB_ACCESS_2_4B_FLAG;

    uint8_t data[4];
    data[0] = val & 0xFF;
    data[1] = (val >> 8) & 0xFF;
    data[2] = (val >> 16) & 0xFF;
    data[3] = (val >> 24) & 0xFF;

    return sdio_write_bytes(SDIO_FUNC_1, offset, data, 4, true);
}

cyw_err_t cyw_backplane_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    cyw_err_t err;
    uint32_t offset;

    while (len > 0) {
        uint32_t chunk = len;
        uint32_t window_offset = addr & SBSDIO_SB_OFT_ADDR_MASK;

        /* Limit to window boundary */
        if (window_offset + chunk > SBSDIO_SB_OFT_ADDR_LIMIT) {
            chunk = SBSDIO_SB_OFT_ADDR_LIMIT - window_offset;
        }

        err = set_backplane_window(addr);
        if (err != CYW_OK) return err;

        offset = window_offset | SBSDIO_SB_ACCESS_2_4B_FLAG;
        err = sdio_read_bytes(SDIO_FUNC_1, offset, data, chunk, true);
        if (err != CYW_OK) return err;

        addr += chunk;
        data += chunk;
        len -= chunk;
    }

    return CYW_OK;
}

cyw_err_t cyw_backplane_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    cyw_err_t err;
    uint32_t offset;

    while (len > 0) {
        uint32_t chunk = len;
        uint32_t window_offset = addr & SBSDIO_SB_OFT_ADDR_MASK;

        /* Limit to window boundary */
        if (window_offset + chunk > SBSDIO_SB_OFT_ADDR_LIMIT) {
            chunk = SBSDIO_SB_OFT_ADDR_LIMIT - window_offset;
        }

        err = set_backplane_window(addr);
        if (err != CYW_OK) return err;

        offset = window_offset | SBSDIO_SB_ACCESS_2_4B_FLAG;
        err = sdio_write_bytes(SDIO_FUNC_1, offset, data, chunk, true);
        if (err != CYW_OK) return err;

        addr += chunk;
        data += chunk;
        len -= chunk;
    }

    return CYW_OK;
}

/*============================================================================
 * Clock Management
 *============================================================================*/

static cyw_err_t request_alp_clock(void)
{
    cyw_err_t err;
    uint8_t val;
    int timeout = 100;

    /* Request ALP clock */
    err = cyw_sdio_write8(SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
                          SBSDIO_ALP_AVAIL_REQ);
    if (err != CYW_OK) return err;

    /* Wait for ALP available */
    while (timeout-- > 0) {
        err = cyw_sdio_read8(SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &val);
        if (err != CYW_OK) return err;

        if (val & SBSDIO_ALP_AVAIL) {
            DBG("ALP clock ready");
            return CYW_OK;
        }
        delay_ms(1);
    }

    ERR("ALP clock timeout");
    return CYW_ERR_TIMEOUT;
}

__attribute__((unused))
static cyw_err_t request_ht_clock(void)
{
    cyw_err_t err;
    uint8_t val;
    int timeout = 500;

    /* Request HT clock */
    err = cyw_sdio_write8(SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
                          SBSDIO_HT_AVAIL_REQ);
    if (err != CYW_OK) return err;

    /* Wait for HT available */
    while (timeout-- > 0) {
        err = cyw_sdio_read8(SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &val);
        if (err != CYW_OK) return err;

        if (val & SBSDIO_HT_AVAIL) {
            DBG("HT clock ready");
            return CYW_OK;
        }
        delay_ms(1);
    }

    ERR("HT clock timeout");
    return CYW_ERR_TIMEOUT;
}

/*============================================================================
 * Chip Detection
 *============================================================================*/

static cyw_err_t detect_chip(void)
{
    cyw_dev_t *dev = &g_cyw_dev;
    cyw_err_t err;
    uint32_t val;

    /* Read chip ID from SDIO core register */
    err = cyw_sdio_read32(SDIO_CORE_CHIPID, &val);
    if (err != CYW_OK) {
        ERR("Failed to read chip ID");
        return err;
    }

    dev->chip.chip_id = val & CYW55500_CHIP_ID_MASK;
    dev->chip.chip_rev = (val & CYW55500_CHIP_REV_MASK) >> CYW55500_CHIP_REV_SHIFT;

    DBG("Chip ID: 0x%04X, Rev: %d", dev->chip.chip_id, dev->chip.chip_rev);

    /* Verify chip ID */
    if (dev->chip.chip_id != CYW55500_CHIP_ID) {
        ERR("Unsupported chip ID: 0x%04X", dev->chip.chip_id);
        return CYW_ERR_INVALID;
    }

    /* Set RAM parameters based on revision */
    dev->chip.ram_base = CYW55500_RAM_START;
    /* RAM size would be detected from core enumeration */

    return CYW_OK;
}

/*============================================================================
 * Core Reset
 *============================================================================*/

static cyw_err_t reset_core(uint32_t core_base, uint32_t prereset, uint32_t reset)
{
    cyw_err_t err;

    /* Put core in reset */
    err = cyw_sdio_write32(core_base + 0x800, /* BCMA_RESET_CTL */
                           1 /* BCMA_RESET_CTL_RESET */);
    if (err != CYW_OK) return err;

    delay_us(10);

    /* Disable core */
    err = cyw_sdio_write32(core_base + 0x408, /* BCMA_IOCTL */
                           prereset | 1 /* BCMA_IOCTL_CLK */);
    if (err != CYW_OK) return err;

    delay_us(10);

    /* Take core out of reset */
    err = cyw_sdio_write32(core_base + 0x800, 0);
    if (err != CYW_OK) return err;

    delay_us(10);

    /* Enable core */
    err = cyw_sdio_write32(core_base + 0x408,
                           reset | 1 /* BCMA_IOCTL_CLK */);
    if (err != CYW_OK) return err;

    delay_us(10);

    return CYW_OK;
}

/*============================================================================
 * Firmware Download
 *============================================================================*/

cyw_err_t cyw_load_firmware(const uint8_t *fw_data, uint32_t fw_size,
                            const uint8_t *nvram_data, uint32_t nvram_size)
{
    cyw_dev_t *dev = &g_cyw_dev;
    cyw_err_t err;
    uint32_t addr;

    if (dev->state < CYW_STATE_INIT) {
        return CYW_ERR_NOT_READY;
    }

    dev->state = CYW_STATE_FW_LOADING;
    DBG("Loading firmware (%u bytes)...", fw_size);

    /* Halt ARM core */
    err = cyw_sdio_write32(dev->core_arm.base + ARMCR4_BANKIDX, 0);
    if (err != CYW_OK) goto error;

    /* Download firmware to RAM */
    addr = dev->chip.ram_base;
    err = cyw_backplane_write(addr, fw_data, fw_size);
    if (err != CYW_OK) {
        ERR("Firmware download failed");
        goto error;
    }

    DBG("Firmware downloaded");

    /* Download NVRAM */
    if (nvram_data && nvram_size > 0) {
        /* NVRAM goes at end of RAM */
        /* Calculate proper NVRAM location based on RAM size */
        addr = NVRAM_DL_ADDR;
        err = cyw_backplane_write(addr, nvram_data, nvram_size);
        if (err != CYW_OK) {
            ERR("NVRAM download failed");
            goto error;
        }

        /* Write NVRAM size at end */
        uint32_t nvram_sz_words = nvram_size / 4;
        nvram_sz_words = (~nvram_sz_words << 16) | nvram_sz_words;
        addr += nvram_size;
        err = cyw_sdio_write32(addr, nvram_sz_words);
        if (err != CYW_OK) goto error;

        DBG("NVRAM downloaded (%u bytes)", nvram_size);
    }

    /* Release ARM core */
    err = reset_core(dev->core_arm.base, 0, 0);
    if (err != CYW_OK) goto error;

    /* Wait for firmware ready */
    int timeout = 200;
    while (timeout-- > 0) {
        uint8_t val;
        err = cyw_sdio_read8(SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &val);
        if (err == CYW_OK && (val & SBSDIO_HT_AVAIL)) {
            break;
        }
        delay_ms(10);
    }

    if (timeout <= 0) {
        ERR("Firmware start timeout");
        err = CYW_ERR_TIMEOUT;
        goto error;
    }

    /* Check for firmware ready in mailbox */
    uint32_t mbox;
    timeout = 100;
    while (timeout-- > 0) {
        err = cyw_sdio_read32(SDIO_CORE_TOHOSTMAILBOXDATA, &mbox);
        if (err != CYW_OK) goto error;

        if (mbox & HMB_DATA_FWREADY) {
            DBG("Firmware ready!");
            dev->state = CYW_STATE_FW_READY;
            return CYW_OK;
        }
        delay_ms(10);
    }

    ERR("Firmware not ready");
    err = CYW_ERR_FW;

error:
    dev->state = CYW_STATE_ERROR;
    return err;
}

/*============================================================================
 * SDPCM Frame Handling
 *============================================================================*/

static cyw_err_t send_sdpcm_frame(uint8_t channel, const uint8_t *data, uint32_t len)
{
    cyw_dev_t *dev = &g_cyw_dev;
    sdpcm_header_t *hdr = (sdpcm_header_t *)dev->tx_buf;
    uint32_t total_len;

    /* Build SDPCM header */
    total_len = SDPCM_HEADER_SIZE + len;
    memset(hdr, 0, SDPCM_HEADER_SIZE);

    hdr->len = total_len;
    hdr->len_check = ~total_len;
    hdr->seq = dev->tx_seq++;
    hdr->channel = channel;
    hdr->data_offset = SDPCM_HEADER_SIZE;

    /* Copy payload */
    if (len > 0 && data != NULL) {
        memcpy(dev->tx_buf + SDPCM_HEADER_SIZE, data, len);
    }

    /* Align to 4 bytes */
    total_len = ALIGN(total_len, 4);

    /* Send via Function 2 */
    return sdio_write_bytes(SDIO_FUNC_2, 0, dev->tx_buf, total_len, true);
}

static cyw_err_t recv_sdpcm_frame(uint8_t *channel, uint8_t *data, uint32_t *len)
{
    cyw_dev_t *dev = &g_cyw_dev;
    sdpcm_header_t *hdr;
    cyw_err_t err;
    uint16_t frame_len;
    uint8_t frame_hdr[4];

    /* Read frame length first */
    err = sdio_read_bytes(SDIO_FUNC_2, 0, frame_hdr, 4, true);
    if (err != CYW_OK) return err;

    frame_len = frame_hdr[0] | (frame_hdr[1] << 8);
    if (frame_len == 0 || frame_len > RX_BUF_SIZE) {
        return CYW_ERR_INVALID;
    }

    /* Read full frame */
    err = sdio_read_bytes(SDIO_FUNC_2, 0, dev->rx_buf, frame_len, true);
    if (err != CYW_OK) return err;

    hdr = (sdpcm_header_t *)dev->rx_buf;

    /* Validate header */
    if ((hdr->len ^ hdr->len_check) != 0xFFFF) {
        ERR("SDPCM header checksum error");
        return CYW_ERR_INVALID;
    }

    /* Update flow control */
    dev->flow_ctrl = hdr->flow_control;
    dev->tx_max = hdr->max_seq;
    dev->rx_seq = hdr->seq;

    /* Extract payload */
    *channel = hdr->channel;
    *len = hdr->len - hdr->data_offset;
    if (*len > 0) {
        memcpy(data, dev->rx_buf + hdr->data_offset, *len);
    }

    return CYW_OK;
}

/*============================================================================
 * BCDC Commands
 *============================================================================*/

cyw_err_t cyw_ioctl(uint32_t cmd, void *data, uint32_t len, bool set)
{
    cyw_dev_t *dev = &g_cyw_dev;
    cyw_err_t err;
    bcdc_header_t *bcdc_tx, *bcdc_rx;
    uint8_t buf[512];
    uint32_t total_len;

    if (dev->state < CYW_STATE_FW_READY) {
        return CYW_ERR_NOT_READY;
    }

    /* Build BCDC header */
    bcdc_tx = (bcdc_header_t *)buf;
    bcdc_tx->cmd = cmd;
    bcdc_tx->len = len;
    bcdc_tx->flags = (BCDC_PROTO_VER << BCDC_FLAG_VER_SHIFT) |
                     (set ? 0x02 : 0) |
                     (dev->reqid++ << 16);
    bcdc_tx->status = 0;

    /* Copy data for SET */
    if (set && len > 0 && data != NULL) {
        memcpy(buf + BCDC_HEADER_SIZE, data, len);
    }

    total_len = BCDC_HEADER_SIZE + len;

    /* Send via control channel */
    err = send_sdpcm_frame(SDPCM_CONTROL_CHANNEL, buf, total_len);
    if (err != CYW_OK) return err;

    /* Wait for response */
    int timeout = 100;
    while (timeout-- > 0) {
        uint8_t channel;
        uint32_t rx_len = sizeof(buf);

        err = recv_sdpcm_frame(&channel, buf, &rx_len);
        if (err == CYW_OK && channel == SDPCM_CONTROL_CHANNEL) {
            bcdc_rx = (bcdc_header_t *)buf;

            /* Check for matching response */
            if ((bcdc_rx->flags >> 16) == (uint32_t)(dev->reqid - 1)) {
                if (bcdc_rx->status != 0) {
                    ERR("IOCTL error: %d", bcdc_rx->status);
                    return CYW_ERROR;
                }

                /* Copy data for GET */
                if (!set && len > 0 && data != NULL) {
                    memcpy(data, buf + BCDC_HEADER_SIZE,
                           (bcdc_rx->len < len) ? bcdc_rx->len : len);
                }
                return CYW_OK;
            }
        }
        delay_ms(1);
    }

    return CYW_ERR_TIMEOUT;
}

cyw_err_t cyw_iovar(const char *name, void *data, uint32_t len, bool set)
{
    uint8_t buf[256];
    uint32_t name_len = strlen(name) + 1;
    uint32_t total_len = name_len + len;

    if (total_len > sizeof(buf)) {
        return CYW_ERR_NOMEM;
    }

    /* Format: name + \0 + data */
    memcpy(buf, name, name_len);
    if (len > 0 && data != NULL) {
        memcpy(buf + name_len, data, len);
    }

    cyw_err_t err = cyw_ioctl(set ? WLC_SET_VAR : WLC_GET_VAR,
                              buf, total_len, set);

    if (err == CYW_OK && !set && data != NULL && len > 0) {
        memcpy(data, buf + name_len, len);
    }

    return err;
}

/*============================================================================
 * Initialization
 *============================================================================*/

static cyw_err_t sdio_init_card(void)
{
    cyw_err_t err;
    uint8_t val;

    /* Enable function 1 */
    if (g_cyw_dev.ops->enable_func) {
        g_cyw_dev.ops->enable_func(SDIO_FUNC_1, true);
    }

    /* Wait for function 1 ready */
    int timeout = 100;
    while (timeout-- > 0) {
        err = cyw_sdio_read8(SDIO_FUNC_0, CCCR_IO_READY, &val);
        if (err == CYW_OK && (val & SDIO_FUNC_READY_1)) {
            break;
        }
        delay_ms(1);
    }
    if (timeout <= 0) {
        ERR("Function 1 not ready");
        return CYW_ERR_TIMEOUT;
    }

    /* Set block sizes */
    if (g_cyw_dev.ops->set_block_size) {
        g_cyw_dev.ops->set_block_size(SDIO_FUNC_1, SDIO_F1_BLOCK_SIZE);
        g_cyw_dev.ops->set_block_size(SDIO_FUNC_2, SDIO_F2_BLOCK_SIZE);
    }

    /* Request ALP clock */
    err = request_alp_clock();
    if (err != CYW_OK) return err;

    /* Detect chip */
    err = detect_chip();
    if (err != CYW_OK) return err;

    /* Enable function 2 */
    if (g_cyw_dev.ops->enable_func) {
        g_cyw_dev.ops->enable_func(SDIO_FUNC_2, true);
    }

    /* Wait for function 2 ready */
    timeout = 100;
    while (timeout-- > 0) {
        err = cyw_sdio_read8(SDIO_FUNC_0, CCCR_IO_READY, &val);
        if (err == CYW_OK && (val & SDIO_FUNC_READY_2)) {
            break;
        }
        delay_ms(1);
    }
    if (timeout <= 0) {
        ERR("Function 2 not ready");
        return CYW_ERR_TIMEOUT;
    }

    /* Set F2 watermark */
    err = cyw_sdio_write8(SDIO_FUNC_1, SBSDIO_WATERMARK, CYW55500_F2_WATERMARK);
    if (err != CYW_OK) return err;

    /* Enable interrupts */
    err = cyw_sdio_write8(SDIO_FUNC_0, CCCR_INT_ENABLE,
                          CCCR_IEN_FUNC0 | CCCR_IEN_FUNC1 | CCCR_IEN_FUNC2);
    if (err != CYW_OK) return err;

    DBG("SDIO card initialized");
    return CYW_OK;
}

cyw_err_t cyw_init(const sdio_host_ops_t *ops)
{
    cyw_err_t err;

    if (ops == NULL) {
        return CYW_ERR_INVALID;
    }

    memset(&g_cyw_dev, 0, sizeof(g_cyw_dev));
    g_cyw_dev.ops = ops;
    g_cyw_dev.state = CYW_STATE_OFF;

    /* Initialize SDIO host */
    if (ops->init) {
        int ret = ops->init();
        if (ret != 0) {
            ERR("SDIO host init failed");
            return CYW_ERR_IO;
        }
    }

    /* Initialize card */
    err = sdio_init_card();
    if (err != CYW_OK) {
        ERR("Card init failed");
        return err;
    }

    g_cyw_dev.state = CYW_STATE_INIT;
    DBG("CYW55500 driver initialized");

    return CYW_OK;
}

void cyw_deinit(void)
{
    cyw_dev_t *dev = &g_cyw_dev;

    if (dev->state != CYW_STATE_OFF) {
        /* Disable interrupts */
        cyw_sdio_write8(SDIO_FUNC_0, CCCR_INT_ENABLE, 0);

        /* Disable functions */
        if (dev->ops->enable_func) {
            dev->ops->enable_func(SDIO_FUNC_2, false);
            dev->ops->enable_func(SDIO_FUNC_1, false);
        }

        /* Deinit host */
        if (dev->ops->deinit) {
            dev->ops->deinit();
        }

        dev->state = CYW_STATE_OFF;
    }
}

/*============================================================================
 * WiFi Operations
 *============================================================================*/

cyw_err_t cyw_up(void)
{
    cyw_dev_t *dev = &g_cyw_dev;

    if (dev->state < CYW_STATE_FW_READY) {
        return CYW_ERR_NOT_READY;
    }

    cyw_err_t err = cyw_ioctl(WLC_UP, NULL, 0, true);
    if (err == CYW_OK) {
        dev->state = CYW_STATE_UP;
    }
    return err;
}

cyw_err_t cyw_down(void)
{
    cyw_dev_t *dev = &g_cyw_dev;

    if (dev->state < CYW_STATE_FW_READY) {
        return CYW_ERR_NOT_READY;
    }

    cyw_err_t err = cyw_ioctl(WLC_DOWN, NULL, 0, true);
    if (err == CYW_OK) {
        dev->state = CYW_STATE_FW_READY;
    }
    return err;
}

cyw_err_t cyw_get_chip_info(cyw_chip_info_t *info)
{
    if (info == NULL) {
        return CYW_ERR_INVALID;
    }
    memcpy(info, &g_cyw_dev.chip, sizeof(cyw_chip_info_t));
    return CYW_OK;
}

cyw_state_t cyw_get_state(void)
{
    return g_cyw_dev.state;
}

void cyw_poll(void)
{
    cyw_dev_t *dev = &g_cyw_dev;

    if (dev->state < CYW_STATE_FW_READY) {
        return;
    }

    /* Check for pending data */
    if (dev->ops->irq_pending && dev->ops->irq_pending()) {
        uint8_t channel;
        uint32_t len = sizeof(dev->rx_buf);

        if (recv_sdpcm_frame(&channel, dev->rx_buf, &len) == CYW_OK) {
            switch (channel) {
                case SDPCM_EVENT_CHANNEL:
                    /* Handle events */
                    DBG("Event received, len=%u", len);
                    break;
                case SDPCM_DATA_CHANNEL:
                    /* Handle data */
                    DBG("Data received, len=%u", len);
                    break;
                default:
                    break;
            }
        }
    }
}

/*============================================================================
 * TODO: WiFi Connection Functions (Not Implemented Yet)
 *============================================================================*/

/*
 * TODO 1: cyw_scan() - Сканирование WiFi сетей
 *
 * int cyw_scan(void *results, int max_results)
 * {
 *     // 1. Установить параметры сканирования
 *     struct wl_escan_params {
 *         uint32_t version;
 *         uint16_t action;      // WL_SCAN_ACTION_START = 1
 *         uint16_t sync_id;
 *         struct wl_scan_params {
 *             wlc_ssid_t ssid;
 *             uint8_t bssid[6];
 *             int8_t bss_type;  // -1 = any
 *             uint8_t scan_type; // 0 = active
 *             int32_t nprobes;
 *             int32_t active_time;
 *             int32_t passive_time;
 *             int32_t home_time;
 *             int32_t channel_num;
 *             uint16_t channel_list[1];
 *         } params;
 *     } scan_params = {0};
 *
 *     scan_params.version = 1;
 *     scan_params.action = 1;
 *     scan_params.params.bss_type = -1;
 *     scan_params.params.nprobes = -1;
 *     scan_params.params.active_time = -1;
 *     scan_params.params.passive_time = -1;
 *     scan_params.params.home_time = -1;
 *
 *     // 2. Запустить сканирование
 *     cyw_iovar("escan", &scan_params, sizeof(scan_params), true);
 *
 *     // 3. Ждать событий ESCAN_RESULT на SDPCM_EVENT_CHANNEL
 *     //    Парсить WLC_E_ESCANRESULT события
 *
 *     // 4. Вернуть количество найденных сетей
 *     return count;
 * }
 */

/*
 * TODO 2: cyw_connect() - Подключение к WiFi сети
 *
 * cyw_err_t cyw_connect(const char *ssid, const char *passphrase)
 * {
 *     // 1. Установить режим инфраструктуры
 *     uint32_t infra = 1;
 *     cyw_ioctl(WLC_SET_INFRA, &infra, 4, true);
 *
 *     // 2. Установить режим аутентификации
 *     uint32_t auth = 0;  // Open System
 *     cyw_ioctl(WLC_SET_AUTH, &auth, 4, true);
 *
 *     // 3. Установить WPA auth
 *     uint32_t wpa_auth = 0x80;  // WPA2-PSK
 *     cyw_iovar("wpa_auth", &wpa_auth, 4, true);
 *
 *     // 4. Установить PMK (пароль -> PSK)
 *     struct {
 *         uint16_t key_len;
 *         uint16_t flags;
 *         uint8_t key[64];
 *     } pmk;
 *     pmk.key_len = strlen(passphrase);
 *     pmk.flags = 0;
 *     memcpy(pmk.key, passphrase, pmk.key_len);
 *     cyw_ioctl(WLC_SET_WSEC_PMK, &pmk, sizeof(pmk), true);
 *
 *     // 5. Установить WSEC (шифрование)
 *     uint32_t wsec = 6;  // AES + TKIP
 *     cyw_ioctl(WLC_SET_WSEC, &wsec, 4, true);
 *
 *     // 6. Установить SSID (это запускает подключение)
 *     struct {
 *         uint32_t ssid_len;
 *         char ssid[32];
 *     } wlc_ssid;
 *     wlc_ssid.ssid_len = strlen(ssid);
 *     memcpy(wlc_ssid.ssid, ssid, wlc_ssid.ssid_len);
 *     cyw_ioctl(WLC_SET_SSID, &wlc_ssid, sizeof(wlc_ssid), true);
 *
 *     // 7. Ждать события WLC_E_LINK или WLC_E_SET_SSID
 *     //    с status = WLC_E_STATUS_SUCCESS
 *
 *     return CYW_OK;
 * }
 */

/*
 * TODO 3: cyw_disconnect() - Отключение от сети
 *
 * cyw_err_t cyw_disconnect(void)
 * {
 *     cyw_ioctl(WLC_DISASSOC, NULL, 0, true);
 *     return CYW_OK;
 * }
 */

/*
 * TODO 4: cyw_is_connected() - Проверка подключения
 *
 * bool cyw_is_connected(void)
 * {
 *     uint8_t bssid[6];
 *     cyw_ioctl(WLC_GET_BSSID, bssid, 6, false);
 *     // Если bssid != 00:00:00:00:00:00, то подключены
 *     return (bssid[0] | bssid[1] | bssid[2] |
 *             bssid[3] | bssid[4] | bssid[5]) != 0;
 * }
 */

/*
 * TODO 5: cyw_get_rssi() - Получить уровень сигнала
 *
 * int cyw_get_rssi(void)
 * {
 *     int32_t rssi = 0;
 *     cyw_ioctl(WLC_GET_RSSI, &rssi, 4, false);
 *     return rssi;
 * }
 */

/*
 * TODO 6: Event Handler - Обработка событий WiFi
 *
 * В cyw_poll() добавить парсинг событий:
 *
 * typedef struct {
 *     uint16_t version;
 *     uint16_t flags;
 *     uint32_t event_type;    // WLC_E_*
 *     uint32_t status;
 *     uint32_t reason;
 *     uint32_t auth_type;
 *     uint32_t datalen;
 *     uint8_t addr[6];
 *     char ifname[16];
 *     uint8_t ifidx;
 *     uint8_t bsscfgidx;
 * } wl_event_msg_t;
 *
 * События:
 *   WLC_E_LINK        = 16  - Link up/down
 *   WLC_E_AUTH        = 3   - Authentication
 *   WLC_E_ASSOC       = 0   - Association
 *   WLC_E_SET_SSID    = 46  - SSID set result
 *   WLC_E_ESCANRESULT = 69  - Scan result
 */

/*
 * TODO 7: IOCTL номера (добавить в cyw55500_regs.h)
 *
 * #define WLC_GET_MAGIC         0
 * #define WLC_GET_VERSION       1
 * #define WLC_UP                2
 * #define WLC_DOWN              3
 * #define WLC_SET_INFRA         20
 * #define WLC_SET_AUTH          22
 * #define WLC_SET_SSID          26
 * #define WLC_SET_WSEC          134
 * #define WLC_SET_WSEC_PMK      268
 * #define WLC_GET_RSSI          127
 * #define WLC_GET_BSSID         23
 * #define WLC_DISASSOC          52
 * #define WLC_SCAN              50
 * #define WLC_GET_VAR           262
 * #define WLC_SET_VAR           263
 */
