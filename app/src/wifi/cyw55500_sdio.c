/**
 * CYW55500 WiFi - SDIO Driver Implementation
 * Zephyr RTOS port for RP2350
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "cyw55500_sdio.h"

LOG_MODULE_REGISTER(cyw55500, LOG_LEVEL_DBG);

/*============================================================================
 * Driver Context
 *============================================================================*/

typedef struct {
    cyw_state_t state;
    cyw_chip_info_t chip;
    uint32_t sbwad;
    bool sbwad_valid;
    uint8_t tx_seq;
    uint8_t rx_seq;
    uint8_t tx_max;
    uint8_t flow_ctrl;
    uint16_t reqid;
    uint8_t tx_buf[TX_BUF_SIZE] __attribute__((aligned(4)));
    uint8_t rx_buf[RX_BUF_SIZE] __attribute__((aligned(4)));
    const sdio_host_ops_t *ops;
} cyw_dev_t;

static cyw_dev_t g_cyw_dev;

/* Scan state */
static struct {
    bool in_progress;
    uint16_t sync_id;
    cyw_scan_result_t results[16];
    int count;
} g_scan_state;

/*============================================================================
 * Helper Functions
 *============================================================================*/

static inline void delay_us(uint32_t us)
{
    if (g_cyw_dev.ops && g_cyw_dev.ops->delay_us) {
        g_cyw_dev.ops->delay_us(us);
    } else {
        k_busy_wait(us);
    }
}

static inline void delay_ms(uint32_t ms)
{
    if (g_cyw_dev.ops && g_cyw_dev.ops->delay_ms) {
        g_cyw_dev.ops->delay_ms(ms);
    } else {
        k_msleep(ms);
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

    if (dev->sbwad_valid && dev->sbwad == window) {
        return CYW_OK;
    }

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

    while (len > 0) {
        uint32_t chunk = len;
        uint32_t window_offset = addr & SBSDIO_SB_OFT_ADDR_MASK;

        if (window_offset + chunk > SBSDIO_SB_OFT_ADDR_LIMIT) {
            chunk = SBSDIO_SB_OFT_ADDR_LIMIT - window_offset;
        }

        err = set_backplane_window(addr);
        if (err != CYW_OK) return err;

        uint32_t offset = window_offset | SBSDIO_SB_ACCESS_2_4B_FLAG;
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

    while (len > 0) {
        uint32_t chunk = len;
        uint32_t window_offset = addr & SBSDIO_SB_OFT_ADDR_MASK;

        if (window_offset + chunk > SBSDIO_SB_OFT_ADDR_LIMIT) {
            chunk = SBSDIO_SB_OFT_ADDR_LIMIT - window_offset;
        }

        err = set_backplane_window(addr);
        if (err != CYW_OK) return err;

        uint32_t offset = window_offset | SBSDIO_SB_ACCESS_2_4B_FLAG;
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

    err = cyw_sdio_write8(SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
                          SBSDIO_ALP_AVAIL_REQ);
    if (err != CYW_OK) return err;

    while (timeout-- > 0) {
        err = cyw_sdio_read8(SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &val);
        if (err != CYW_OK) return err;

        if (val & SBSDIO_ALP_AVAIL) {
            LOG_DBG("ALP clock ready");
            return CYW_OK;
        }
        delay_ms(1);
    }

    LOG_ERR("ALP clock timeout");
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

    err = cyw_sdio_read32(0x18000000, &val);
    if (err != CYW_OK) {
        LOG_ERR("Failed to read chip ID");
        return err;
    }

    dev->chip.chip_id = val & CYW55500_CHIP_ID_MASK;
    dev->chip.chip_rev = (val & CYW55500_CHIP_REV_MASK) >> CYW55500_CHIP_REV_SHIFT;

    LOG_INF("Chip ID: 0x%04X, Rev: %d", dev->chip.chip_id, dev->chip.chip_rev);

    dev->chip.ram_base = CYW55500_RAM_START;

    return CYW_OK;
}

/*============================================================================
 * SDPCM Frame Handling
 *============================================================================*/

static cyw_err_t send_sdpcm_frame(uint8_t channel, const uint8_t *data, uint32_t len)
{
    cyw_dev_t *dev = &g_cyw_dev;
    sdpcm_header_t *hdr = (sdpcm_header_t *)dev->tx_buf;
    uint32_t total_len;

    total_len = SDPCM_HEADER_SIZE + len;
    memset(hdr, 0, SDPCM_HEADER_SIZE);

    hdr->len = total_len;
    hdr->len_check = ~total_len;
    hdr->seq = dev->tx_seq++;
    hdr->channel = channel;
    hdr->data_offset = SDPCM_HEADER_SIZE;

    if (len > 0 && data != NULL) {
        memcpy(dev->tx_buf + SDPCM_HEADER_SIZE, data, len);
    }

    total_len = ALIGN(total_len, 4);

    return sdio_write_bytes(SDIO_FUNC_2, 0, dev->tx_buf, total_len, true);
}

static cyw_err_t recv_sdpcm_frame(uint8_t *channel, uint8_t *data, uint32_t *len)
{
    cyw_dev_t *dev = &g_cyw_dev;
    sdpcm_header_t *hdr;
    cyw_err_t err;
    uint8_t frame_hdr[4];

    err = sdio_read_bytes(SDIO_FUNC_2, 0, frame_hdr, 4, true);
    if (err != CYW_OK) return err;

    uint16_t frame_len = frame_hdr[0] | (frame_hdr[1] << 8);
    if (frame_len == 0 || frame_len > RX_BUF_SIZE) {
        return CYW_ERR_INVALID;
    }

    err = sdio_read_bytes(SDIO_FUNC_2, 0, dev->rx_buf, frame_len, true);
    if (err != CYW_OK) return err;

    hdr = (sdpcm_header_t *)dev->rx_buf;

    if ((hdr->len ^ hdr->len_check) != 0xFFFF) {
        LOG_ERR("SDPCM header checksum error");
        return CYW_ERR_INVALID;
    }

    dev->flow_ctrl = hdr->flow_control;
    dev->tx_max = hdr->max_seq;
    dev->rx_seq = hdr->seq;

    *channel = hdr->channel;
    *len = hdr->len - hdr->data_offset;
    if (*len > 0) {
        memcpy(data, dev->rx_buf + hdr->data_offset, *len);
    }

    return CYW_OK;
}

/*============================================================================
 * IOCTL Commands
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

    bcdc_tx = (bcdc_header_t *)buf;
    bcdc_tx->cmd = cmd;
    bcdc_tx->len = len;
    bcdc_tx->flags = (BCDC_PROTO_VER << BCDC_FLAG_VER_SHIFT) |
                     (set ? 0x02 : 0) |
                     (dev->reqid++ << 16);
    bcdc_tx->status = 0;

    if (set && len > 0 && data != NULL) {
        memcpy(buf + BCDC_HEADER_SIZE, data, len);
    }

    total_len = BCDC_HEADER_SIZE + len;

    err = send_sdpcm_frame(SDPCM_CONTROL_CHANNEL, buf, total_len);
    if (err != CYW_OK) return err;

    int timeout = 100;
    while (timeout-- > 0) {
        uint8_t channel;
        uint32_t rx_len = sizeof(buf);

        err = recv_sdpcm_frame(&channel, buf, &rx_len);
        if (err == CYW_OK && channel == SDPCM_CONTROL_CHANNEL) {
            bcdc_rx = (bcdc_header_t *)buf;

            if ((bcdc_rx->flags >> 16) == (uint32_t)(dev->reqid - 1)) {
                if (bcdc_rx->status != 0) {
                    LOG_ERR("IOCTL error: %d", bcdc_rx->status);
                    return CYW_ERROR;
                }

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

    if (g_cyw_dev.ops->enable_func) {
        g_cyw_dev.ops->enable_func(SDIO_FUNC_1, true);
    }

    int timeout = 100;
    while (timeout-- > 0) {
        err = cyw_sdio_read8(SDIO_FUNC_0, CCCR_IO_READY, &val);
        if (err == CYW_OK && (val & SDIO_FUNC_READY_1)) {
            break;
        }
        delay_ms(1);
    }
    if (timeout <= 0) {
        LOG_ERR("Function 1 not ready");
        return CYW_ERR_TIMEOUT;
    }

    if (g_cyw_dev.ops->set_block_size) {
        g_cyw_dev.ops->set_block_size(SDIO_FUNC_1, SDIO_F1_BLOCK_SIZE);
        g_cyw_dev.ops->set_block_size(SDIO_FUNC_2, SDIO_F2_BLOCK_SIZE);
    }

    err = request_alp_clock();
    if (err != CYW_OK) return err;

    err = detect_chip();
    if (err != CYW_OK) return err;

    if (g_cyw_dev.ops->enable_func) {
        g_cyw_dev.ops->enable_func(SDIO_FUNC_2, true);
    }

    timeout = 100;
    while (timeout-- > 0) {
        err = cyw_sdio_read8(SDIO_FUNC_0, CCCR_IO_READY, &val);
        if (err == CYW_OK && (val & SDIO_FUNC_READY_2)) {
            break;
        }
        delay_ms(1);
    }
    if (timeout <= 0) {
        LOG_ERR("Function 2 not ready");
        return CYW_ERR_TIMEOUT;
    }

    err = cyw_sdio_write8(SDIO_FUNC_1, SBSDIO_WATERMARK, CYW55500_F2_WATERMARK);
    if (err != CYW_OK) return err;

    err = cyw_sdio_write8(SDIO_FUNC_0, CCCR_INT_ENABLE,
                          CCCR_IEN_FUNC0 | CCCR_IEN_FUNC1 | CCCR_IEN_FUNC2);
    if (err != CYW_OK) return err;

    LOG_INF("SDIO card initialized");
    return CYW_OK;
}

cyw_err_t cyw_init(const sdio_host_ops_t *ops)
{
    cyw_err_t err;

    if (ops == NULL) {
        return CYW_ERR_INVALID;
    }

    memset(&g_cyw_dev, 0, sizeof(g_cyw_dev));
    memset(&g_scan_state, 0, sizeof(g_scan_state));
    g_cyw_dev.ops = ops;
    g_cyw_dev.state = CYW_STATE_OFF;

    if (ops->init) {
        int ret = ops->init();
        if (ret != 0) {
            LOG_ERR("SDIO host init failed: %d", ret);
            return CYW_ERR_IO;
        }
    }

    err = sdio_init_card();
    if (err != CYW_OK) {
        LOG_ERR("Card init failed");
        return err;
    }

    g_cyw_dev.state = CYW_STATE_INIT;
    LOG_INF("CYW55500 driver initialized");

    return CYW_OK;
}

void cyw_deinit(void)
{
    cyw_dev_t *dev = &g_cyw_dev;

    if (dev->state != CYW_STATE_OFF) {
        cyw_sdio_write8(SDIO_FUNC_0, CCCR_INT_ENABLE, 0);

        if (dev->ops->enable_func) {
            dev->ops->enable_func(SDIO_FUNC_2, false);
            dev->ops->enable_func(SDIO_FUNC_1, false);
        }

        if (dev->ops->deinit) {
            dev->ops->deinit();
        }

        dev->state = CYW_STATE_OFF;
    }
}

/*============================================================================
 * Firmware Loading
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
    LOG_INF("Loading firmware (%u bytes)...", fw_size);

    /* Download firmware to RAM */
    addr = dev->chip.ram_base;
    err = cyw_backplane_write(addr, fw_data, fw_size);
    if (err != CYW_OK) {
        LOG_ERR("Firmware download failed");
        goto error;
    }
    LOG_DBG("Firmware downloaded to 0x%08X", addr);

    /* Download NVRAM */
    if (nvram_data && nvram_size > 0) {
        addr = NVRAM_DL_ADDR;
        err = cyw_backplane_write(addr, nvram_data, nvram_size);
        if (err != CYW_OK) {
            LOG_ERR("NVRAM download failed");
            goto error;
        }

        uint32_t nvram_sz_words = nvram_size / 4;
        nvram_sz_words = (~nvram_sz_words << 16) | nvram_sz_words;
        addr += nvram_size;
        err = cyw_sdio_write32(addr, nvram_sz_words);
        if (err != CYW_OK) goto error;

        LOG_DBG("NVRAM downloaded (%u bytes)", nvram_size);
    }

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
        LOG_ERR("Firmware start timeout");
        err = CYW_ERR_TIMEOUT;
        goto error;
    }

    /* Check firmware ready mailbox */
    uint32_t mbox;
    timeout = 100;
    while (timeout-- > 0) {
        err = cyw_sdio_read32(0x18002048, &mbox);
        if (err != CYW_OK) goto error;

        if (mbox & HMB_DATA_FWREADY) {
            LOG_INF("Firmware ready!");
            dev->state = CYW_STATE_FW_READY;
            return CYW_OK;
        }
        delay_ms(10);
    }

    LOG_ERR("Firmware not ready");
    err = CYW_ERR_FW;

error:
    dev->state = CYW_STATE_ERROR;
    return err;
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

/*============================================================================
 * Scan
 *============================================================================*/

int cyw_scan(cyw_scan_result_t *results, int max_results)
{
    cyw_dev_t *dev = &g_cyw_dev;

    if (dev->state < CYW_STATE_UP) {
        return CYW_ERR_NOT_READY;
    }

    memset(&g_scan_state, 0, sizeof(g_scan_state));
    g_scan_state.sync_id = (uint16_t)(dev->reqid & 0xFFFF);
    g_scan_state.in_progress = true;

    struct __attribute__((packed)) {
        uint32_t version;
        uint16_t action;
        uint16_t sync_id;
        uint32_t ssid_len;
        char ssid[32];
        uint8_t bssid[6];
        int8_t bss_type;
        uint8_t scan_type;
        int32_t nprobes;
        int32_t active_time;
        int32_t passive_time;
        int32_t home_time;
        int32_t channel_num;
        uint16_t channel_list[1];
    } params;

    memset(&params, 0, sizeof(params));
    params.version = 1;
    params.action = 1;
    params.sync_id = g_scan_state.sync_id;
    memset(params.bssid, 0xFF, 6);
    params.bss_type = 2;
    params.scan_type = 0;
    params.nprobes = 2;
    params.active_time = 40;
    params.passive_time = 110;
    params.home_time = 45;
    params.channel_num = 0;

    LOG_INF("Starting scan...");

    cyw_err_t err = cyw_iovar("escan", &params, sizeof(params), true);
    if (err != CYW_OK) {
        g_scan_state.in_progress = false;
        LOG_ERR("escan failed: %d", err);
        return err;
    }

    /* Wait for scan to complete */
    int timeout = 10000;
    while (g_scan_state.in_progress && timeout > 0) {
        cyw_poll();
        delay_ms(10);
        timeout -= 10;
    }

    if (g_scan_state.in_progress) {
        g_scan_state.in_progress = false;
        LOG_WRN("Scan timeout");
    }

    int count = g_scan_state.count;
    if (count > max_results) count = max_results;

    if (results && count > 0) {
        memcpy(results, g_scan_state.results, count * sizeof(cyw_scan_result_t));
    }

    LOG_INF("Scan complete, found %d networks", count);
    return count;
}

/*============================================================================
 * Connect/Disconnect
 *============================================================================*/

cyw_err_t cyw_connect(const char *ssid, const char *passphrase)
{
    cyw_dev_t *dev = &g_cyw_dev;
    cyw_err_t err;

    if (dev->state < CYW_STATE_UP) {
        return CYW_ERR_NOT_READY;
    }

    uint32_t infra = 1;
    err = cyw_ioctl(WLC_SET_INFRA, &infra, sizeof(infra), true);
    if (err != CYW_OK) return err;

    uint32_t auth = 0;
    err = cyw_ioctl(WLC_SET_AUTH, &auth, sizeof(auth), true);
    if (err != CYW_OK) return err;

    uint32_t wpa_auth = 0x80;
    err = cyw_iovar("wpa_auth", &wpa_auth, sizeof(wpa_auth), true);
    if (err != CYW_OK) return err;

    uint32_t wsec = 4;
    err = cyw_ioctl(WLC_SET_WSEC, &wsec, sizeof(wsec), true);
    if (err != CYW_OK) return err;

    if (passphrase && strlen(passphrase) > 0) {
        struct __attribute__((packed)) {
            uint16_t key_len;
            uint16_t flags;
            uint8_t key[64];
        } pmk;

        memset(&pmk, 0, sizeof(pmk));
        pmk.key_len = strlen(passphrase);
        memcpy(pmk.key, passphrase, pmk.key_len);

        err = cyw_ioctl(268, &pmk, sizeof(pmk), true);
        if (err != CYW_OK) return err;
    }

    struct __attribute__((packed)) {
        uint32_t ssid_len;
        char ssid[32];
    } wlc_ssid;

    memset(&wlc_ssid, 0, sizeof(wlc_ssid));
    wlc_ssid.ssid_len = strlen(ssid);
    if (wlc_ssid.ssid_len > 32) wlc_ssid.ssid_len = 32;
    memcpy(wlc_ssid.ssid, ssid, wlc_ssid.ssid_len);

    LOG_INF("Connecting to %s...", ssid);
    err = cyw_ioctl(WLC_SET_SSID, &wlc_ssid, sizeof(wlc_ssid), true);
    if (err != CYW_OK) return err;

    int timeout = 10000;
    while (timeout > 0) {
        cyw_poll();
        if (cyw_is_connected()) {
            LOG_INF("Connected!");
            return CYW_OK;
        }
        delay_ms(100);
        timeout -= 100;
    }

    LOG_ERR("Connection timeout");
    return CYW_ERR_TIMEOUT;
}

cyw_err_t cyw_disconnect(void)
{
    return cyw_ioctl(WLC_DISASSOC, NULL, 0, true);
}

bool cyw_is_connected(void)
{
    uint8_t bssid[6] = {0};
    cyw_err_t err = cyw_ioctl(WLC_GET_BSSID, bssid, sizeof(bssid), false);

    if (err != CYW_OK) return false;

    return (bssid[0] | bssid[1] | bssid[2] |
            bssid[3] | bssid[4] | bssid[5]) != 0;
}

int cyw_get_rssi(void)
{
    int32_t rssi = 0;
    cyw_err_t err = cyw_ioctl(WLC_GET_RSSI, &rssi, sizeof(rssi), false);
    return (err == CYW_OK) ? rssi : 0;
}

/*============================================================================
 * Event Polling
 *============================================================================*/

void cyw_poll(void)
{
    cyw_dev_t *dev = &g_cyw_dev;

    if (dev->state < CYW_STATE_FW_READY) {
        return;
    }

    uint8_t channel;
    uint32_t len = sizeof(dev->rx_buf);

    if (recv_sdpcm_frame(&channel, dev->rx_buf, &len) != CYW_OK) {
        return;
    }

    switch (channel) {
        case SDPCM_EVENT_CHANNEL:
            LOG_DBG("Event received, len=%u", len);
            /* TODO: Parse events */
            break;

        case SDPCM_DATA_CHANNEL:
            LOG_DBG("Data received, len=%u", len);
            break;

        default:
            break;
    }
}
