/**
 * CYW55500 WiFi - SDIO Communication Test
 * Step 1: Verify basic SDIO commands work
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/*============================================================================
 * GPIO Pin Definitions (Quectel FCS96xN)
 *============================================================================*/

#define GPIO0_NODE DT_NODELABEL(gpio0)

#define PIN_CLK      18
#define PIN_CMD      19
#define PIN_D0       20
#define PIN_D1       21
#define PIN_D2       22
#define PIN_D3       26
#define PIN_REG_ON   27

static const struct device *gpio_dev;

/*============================================================================
 * Low-level GPIO
 *============================================================================*/

static inline void clk_high(void) { gpio_pin_set(gpio_dev, PIN_CLK, 1); }
static inline void clk_low(void)  { gpio_pin_set(gpio_dev, PIN_CLK, 0); }
static inline void cmd_high(void) { gpio_pin_set(gpio_dev, PIN_CMD, 1); }
static inline void cmd_low(void)  { gpio_pin_set(gpio_dev, PIN_CMD, 0); }
static inline int  cmd_read(void) { return gpio_pin_get(gpio_dev, PIN_CMD); }

static inline void cmd_output(void) {
    gpio_pin_configure(gpio_dev, PIN_CMD, GPIO_OUTPUT);
}
static inline void cmd_input(void) {
    gpio_pin_configure(gpio_dev, PIN_CMD, GPIO_INPUT | GPIO_PULL_UP);
}

static void clock_cycle(void)
{
    clk_high();
    k_busy_wait(5);  /* ~100kHz for reliable init */
    clk_low();
    k_busy_wait(5);
}

/*============================================================================
 * CRC7 Calculation
 *============================================================================*/

static uint8_t crc7(const uint8_t *data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        uint8_t d = data[i];
        for (int j = 0; j < 8; j++) {
            crc <<= 1;
            if ((d & 0x80) ^ (crc & 0x80)) {
                crc ^= 0x09;
            }
            d <<= 1;
        }
    }
    return (crc << 1) | 1;
}

/*============================================================================
 * Send Command
 *============================================================================*/

static void send_bits(uint64_t data, int bits)
{
    cmd_output();
    for (int i = bits - 1; i >= 0; i--) {
        if (data & (1ULL << i)) {
            cmd_high();
        } else {
            cmd_low();
        }
        k_busy_wait(2);  /* Setup time: let CMD settle before CLK edge */
        clock_cycle();
    }
}

static int wait_response_start(void)
{
    cmd_input();

    /* Wait for start bit (0) - card pulls CMD low */
    for (int i = 0; i < 5000; i++) {  /* ~50ms timeout at ~100kHz */
        clk_high();
        k_busy_wait(5);
        int bit = cmd_read();
        clk_low();
        k_busy_wait(5);

        if (bit == 0) {
            return 0;  /* Got start bit */
        }
    }
    return -1;  /* Timeout */
}

static uint64_t receive_bits(int bits)
{
    uint64_t data = 0;
    for (int i = 0; i < bits; i++) {
        clk_high();
        k_busy_wait(5);
        data <<= 1;
        if (cmd_read()) {
            data |= 1;
        }
        clk_low();
        k_busy_wait(5);
    }
    return data;
}

static int sdio_send_cmd(uint8_t cmd, uint32_t arg, uint64_t *response)
{
    uint8_t buf[5];
    buf[0] = 0x40 | cmd;
    buf[1] = (arg >> 24) & 0xFF;
    buf[2] = (arg >> 16) & 0xFF;
    buf[3] = (arg >> 8) & 0xFF;
    buf[4] = arg & 0xFF;
    uint8_t crc = crc7(buf, 5);

    /* Build 48-bit command frame */
    /* Format: start(0) + tx(1) + cmd(6) + arg(32) + crc7(7) + end(1) */
    uint64_t frame = 0;
    /* Start bit = 0 (NOT 1!) */
    frame |= (1ULL << 46);           /* Direction = 1 (host to card) */
    frame |= ((uint64_t)cmd << 40);  /* Command index */
    frame |= ((uint64_t)arg << 8);   /* Argument */
    frame |= crc;                    /* CRC7 + end bit */

    LOG_INF("CMD%d: arg=0x%08x", cmd, arg);

    /* Send command */
    send_bits(frame, 48);

    /* Wait for response */
    if (wait_response_start() < 0) {
        LOG_WRN("CMD%d: no response (timeout)", cmd);
        return -1;
    }

    /* Read response (47 bits after start bit) */
    uint64_t resp = receive_bits(47);

    LOG_INF("CMD%d: response=0x%012llx", cmd, resp);

    if (response) {
        /* Return full response, shifted to remove end bit and reserved bits
         * R4 (CMD5): bit 32 = C (ready), bits 30-28 = num_io, bits 23-0 = OCR
         * R6 (CMD3): bits 31-16 = RCA, bits 15-0 = status
         */
        *response = resp >> 8;
    }

    /* Extra clocks */
    for (int i = 0; i < 8; i++) {
        clock_cycle();
    }

    return 0;
}

/*============================================================================
 * CMD52 - Single byte I/O
 *============================================================================*/

static int cmd52_read(uint8_t func, uint32_t addr, uint8_t *val)
{
    uint32_t arg = 0;
    arg |= ((func & 0x7) << 28);
    arg |= ((addr & 0x1FFFF) << 9);

    uint64_t response;
    int ret = sdio_send_cmd(52, arg, &response);
    if (ret < 0) return ret;

    uint8_t flags = (response >> 8) & 0xFF;
    *val = response & 0xFF;

    LOG_INF("CMD52 READ: func=%d addr=0x%05x -> val=0x%02x flags=0x%02x",
            func, addr, *val, flags);

    return (flags & 0xCB) ? -1 : 0;
}

static int cmd52_write(uint8_t func, uint32_t addr, uint8_t val)
{
    uint32_t arg = 0;
    arg |= (1UL << 31);              /* Write */
    arg |= ((func & 0x7) << 28);
    arg |= ((addr & 0x1FFFF) << 9);
    arg |= val;

    uint64_t response;
    int ret = sdio_send_cmd(52, arg, &response);
    if (ret < 0) return ret;

    uint8_t flags = (response >> 8) & 0xFF;

    LOG_INF("CMD52 WRITE: func=%d addr=0x%05x val=0x%02x -> flags=0x%02x",
            func, addr, val, flags);

    return (flags & 0xCB) ? -1 : 0;
}

/*============================================================================
 * SDIO Card Initialization
 *============================================================================*/

static int sdio_init_card(void)
{
    uint64_t response;
    int ret;

    LOG_INF("=== SDIO Card Initialization ===");

    /* Send 100+ clock cycles with CMD high (card init per SD spec) */
    cmd_output();
    cmd_high();
    clk_low();
    k_msleep(1);
    for (int i = 0; i < 100; i++) {
        clock_cycle();
    }
    k_msleep(10);

    /* CMD0 - Reset (send multiple times to ensure card sees it) */
    LOG_INF("Sending CMD0 (GO_IDLE)...");
    for (int attempt = 0; attempt < 3; attempt++) {
        send_bits(0x400000000095ULL, 48);  /* CMD0 with valid CRC */
        k_msleep(10);
    }

    /* CMD5 - IO_SEND_OP_COND (query) - retry multiple times */
    LOG_INF("Sending CMD5 (IO_SEND_OP_COND)...");
    for (int attempt = 0; attempt < 5; attempt++) {
        LOG_INF("CMD5 attempt %d...", attempt + 1);
        ret = sdio_send_cmd(5, 0, &response);
        if (ret == 0) {
            LOG_INF("CMD5 got response!");
            break;
        }
        k_msleep(50);  /* Wait longer between attempts */
    }
    if (ret < 0) {
        LOG_ERR("CMD5 failed after 5 attempts - is WiFi module connected?");
        return -1;
    }

    /* R4 response after >>7: OCR at bits 24-1, num_io at bits 31-29, C at bit 32 */
    uint32_t ocr = (response >> 1) & 0x00FFFFFF;
    uint8_t num_io = (response >> 29) & 0x7;
    LOG_INF("OCR=0x%06x, IO functions=%d", ocr, num_io);

    /* CMD5 with voltage - C flag is at bit 32 after shift */
    LOG_INF("Sending CMD5 with voltage window...");
    for (int i = 0; i < 100; i++) {
        ret = sdio_send_cmd(5, ocr, &response);
        if (ret < 0) return ret;

        if (response & (1ULL << 32)) {  /* C flag at bit 32 */
            LOG_INF("Card ready! OCR=0x%08llx", response);
            break;
        }
        k_msleep(10);
    }

    if (!(response & (1ULL << 32))) {
        LOG_ERR("Card not ready");
        return -1;
    }

    /* CMD3 - Get relative address */
    LOG_INF("Sending CMD3 (SEND_RELATIVE_ADDR)...");
    ret = sdio_send_cmd(3, 0, &response);
    if (ret < 0) {
        LOG_ERR("CMD3 failed");
        return -1;
    }

    uint16_t rca = (response >> 17) & 0xFFFF;  /* R6: RCA is at bits 39-24 of raw, 32-17 after >>7 */
    LOG_INF("RCA = 0x%04x", rca);

    /* Wait for card to be ready after CMD3 */
    k_msleep(10);

    /* CMD7 - Select card */
    LOG_INF("Sending CMD7 (SELECT_CARD) with RCA=0x%04x...", rca);
    ret = sdio_send_cmd(7, (uint32_t)rca << 16, &response);
    if (ret < 0) {
        LOG_ERR("CMD7 failed");
        return -1;
    }
    LOG_INF("Card selected!");

    return 0;
}

/*============================================================================
 * Read CCCR (Card Common Control Registers)
 *============================================================================*/

static void read_cccr(void)
{
    uint8_t val;

    LOG_INF("=== Reading CCCR ===");

    if (cmd52_read(0, 0x00, &val) == 0) {
        LOG_INF("CCCR/SDIO Rev: 0x%02x (CCCR=%d.%d, SDIO=%d.%d)",
                val, (val >> 4) & 0xF, val & 0xF,
                (val >> 4) & 0xF, val & 0xF);
    }

    if (cmd52_read(0, 0x01, &val) == 0) {
        LOG_INF("SD Spec Rev: 0x%02x", val);
    }

    if (cmd52_read(0, 0x02, &val) == 0) {
        LOG_INF("I/O Enable: 0x%02x", val);
    }

    if (cmd52_read(0, 0x03, &val) == 0) {
        LOG_INF("I/O Ready: 0x%02x", val);
    }

    if (cmd52_read(0, 0x04, &val) == 0) {
        LOG_INF("Int Enable: 0x%02x", val);
    }

    if (cmd52_read(0, 0x05, &val) == 0) {
        LOG_INF("Int Pending: 0x%02x", val);
    }

    if (cmd52_read(0, 0x06, &val) == 0) {
        LOG_INF("I/O Abort: 0x%02x", val);
    }

    if (cmd52_read(0, 0x07, &val) == 0) {
        LOG_INF("Bus Interface: 0x%02x", val);
    }

    if (cmd52_read(0, 0x08, &val) == 0) {
        LOG_INF("Card Capability: 0x%02x", val);
    }

    if (cmd52_read(0, 0x13, &val) == 0) {
        LOG_INF("High Speed: 0x%02x", val);
    }
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void)
{
    LOG_INF("========================================");
    LOG_INF("CYW55500 SDIO Communication Test");
    LOG_INF("Platform: RP2350 (Pico 2) RISC-V");
    LOG_INF("========================================");

    /* Get GPIO device */
    gpio_dev = DEVICE_DT_GET(GPIO0_NODE);
    if (!device_is_ready(gpio_dev)) {
        LOG_ERR("GPIO device not ready!");
        return -1;
    }
    LOG_INF("GPIO device ready");

    /* Configure pins */
    gpio_pin_configure(gpio_dev, PIN_CLK, GPIO_OUTPUT);
    gpio_pin_configure(gpio_dev, PIN_CMD, GPIO_OUTPUT);
    gpio_pin_configure(gpio_dev, PIN_D0, GPIO_INPUT);
    gpio_pin_configure(gpio_dev, PIN_D1, GPIO_INPUT);
    gpio_pin_configure(gpio_dev, PIN_D2, GPIO_INPUT);
    gpio_pin_configure(gpio_dev, PIN_D3, GPIO_INPUT);
    gpio_pin_configure(gpio_dev, PIN_REG_ON, GPIO_OUTPUT);

    LOG_INF("GPIO configured:");
    LOG_INF("  CLK=GP%d, CMD=GP%d", PIN_CLK, PIN_CMD);
    LOG_INF("  D0=GP%d, D1=GP%d, D2=GP%d, D3=GP%d",
            PIN_D0, PIN_D1, PIN_D2, PIN_D3);
    LOG_INF("  REG_ON=GP%d", PIN_REG_ON);

    /* Power cycle WiFi chip */
    LOG_INF("Power cycling WiFi module...");
    gpio_pin_set(gpio_dev, PIN_REG_ON, 0);
    k_msleep(200);
    gpio_pin_set(gpio_dev, PIN_REG_ON, 1);
    k_msleep(500);  /* CYW55500 needs time after power on - 500ms to be safe */
    LOG_INF("WiFi module powered on");

    /* GPIO test disabled - was interfering with WiFi module init */

    /* Initialize for SDIO:
     * - CLK low
     * - CMD high
     * - D3 high (selects SDIO mode vs SPI mode)
     */
    clk_low();
    cmd_output();
    cmd_high();
    gpio_pin_configure(gpio_dev, PIN_D3, GPIO_OUTPUT);
    gpio_pin_set(gpio_dev, PIN_D3, 1);  /* D3 high = SDIO mode */

    /* Debug: check if we can read CMD line */
    cmd_input();
    int cmd_state = cmd_read();
    LOG_INF("CMD line state after power on: %d (should be 1 with pull-up)", cmd_state);

    /* Initialize SDIO card */
    if (sdio_init_card() < 0) {
        LOG_ERR("SDIO init failed!");
        LOG_INF("Troubleshooting hints:");
        LOG_INF("  1. Check wiring: CLK->GP18, CMD->GP19, D0->GP20");
        LOG_INF("  2. Verify 3.3V power to WiFi module");
        LOG_INF("  3. Add 10k pull-up resistors on CMD and D0-D3 if not present");
        LOG_INF("  4. Check REG_ON polarity (should be active high)");
        goto done;
    }

    /* Read CCCR registers */
    read_cccr();

    /* Try to enable Function 1 (backplane) */
    LOG_INF("=== Enabling Function 1 ===");
    if (cmd52_write(0, 0x02, 0x02) == 0) {
        LOG_INF("Wrote IOE=0x02");

        /* Wait for ready */
        for (int i = 0; i < 100; i++) {
            uint8_t val;
            if (cmd52_read(0, 0x03, &val) == 0) {
                if (val & 0x02) {
                    LOG_INF("Function 1 ready!");
                    break;
                }
            }
            k_msleep(10);
        }
    }

    /*
     * ==========================================================================
     * TODO: Implementation steps after CMD0/CMD3/CMD5/CMD7/CMD52 work
     * ==========================================================================
     *
     * PHASE 1: CMD53 Multi-byte Transfer
     * ---------------------------------------------------------------------------
     * [ ] 1.1 Implement cmd53_read(func, addr, data, len, incr_addr)
     *         - Build CMD53 argument: R/W(1) + func(3) + block_mode(1) +
     *           op_code(1) + addr(17) + count(9)
     *         - Send command, wait R5 response
     *         - Wait for data start token (0) on D0
     *         - Read data bytes on D0 line
     *         - Skip CRC16 (16 clock cycles)
     *
     * [ ] 1.2 Implement cmd53_write(func, addr, data, len, incr_addr)
     *         - Same argument format with R/W=1
     *         - Send command, wait R5 response
     *         - Send data start token (0)
     *         - Send data bytes on D0 line
     *         - Send CRC16 (can be dummy 0xFFFF)
     *         - Wait for card busy (D0 low → high)
     *
     * PHASE 2: Backplane Access Setup
     * ---------------------------------------------------------------------------
     * [ ] 2.1 Request ALP clock
     *         - CMD52 write: F1 reg 0x1000E (CHIPCLKCSR) = 0x08 (ALP_AVAIL_REQ)
     *         - Poll: CMD52 read 0x1000E until bit 6 (ALP_AVAIL) = 1
     *         - Timeout: 100ms max
     *
     * [ ] 2.2 Implement set_backplane_window(addr)
     *         - CMD52 write: F1 reg 0x1000A = (addr >> 8) & 0xFF   // SBADDRLOW
     *         - CMD52 write: F1 reg 0x1000B = (addr >> 16) & 0xFF  // SBADDRMID
     *         - CMD52 write: F1 reg 0x1000C = (addr >> 24) & 0xFF  // SBADDRHIGH
     *         - Cache current window to avoid redundant writes
     *
     * [ ] 2.3 Implement backplane_read32(addr) / backplane_write32(addr, val)
     *         - Call set_backplane_window(addr)
     *         - Offset = (addr & 0x7FFF) | 0x8000  // 15-bit offset + 32-bit flag
     *         - CMD53 read/write 4 bytes to F1 at offset
     *
     * PHASE 3: Chip Identification
     * ---------------------------------------------------------------------------
     * [ ] 3.1 Read Chip ID
     *         - backplane_read32(0x18000000) → chip_id (lower 16 bits)
     *         - Expected: 0x55500 for CYW55500
     *         - Also read chip revision from bits [19:16]
     *
     * [ ] 3.2 Read chip capabilities
     *         - backplane_read32(0x18000004) → capabilities
     *         - backplane_read32(0x180000FC) → enum base for core enumeration
     *
     * [ ] 3.3 Determine RAM base address
     *         - CYW55500-A0 (rev 0): RAM base = 0x3A0AB4
     *         - CYW55500-A1 (rev 1): RAM base = 0x3A1020
     *
     * PHASE 4: Firmware Download
     * ---------------------------------------------------------------------------
     * [ ] 4.1 Store firmware in RP2350 flash
     *         - Create flash partition at 0x10080000 (1MB)
     *         - Flash cyfmac55500-sdio.trxse via picotool
     *         - Access via XIP: const uint8_t *fw = (void*)0x10080000
     *
     * [ ] 4.2 Parse TRX header (optional verification)
     *         - Check magic: *(uint32_t*)fw == 0x30524448 ("HDR0")
     *         - Read fw_len from header offset 4
     *         - Verify CRC32 if needed
     *
     * [ ] 4.3 Download firmware to chip RAM
     *         - Start address: ram_base (0x3A0AB4 or 0x3A1020)
     *         - Loop: for each 64-byte block:
     *           - set_backplane_window(ram_base + offset)
     *           - cmd53_write(1, bp_offset, &fw[offset], 64, true)
     *         - Total size: ~550KB, takes ~10-20 seconds with bit-bang
     *
     * [ ] 4.4 Verify firmware download (optional)
     *         - Read back and compare first/last 1KB
     *         - Or rely on firmware's own CRC check
     *
     * PHASE 5: NVRAM Download
     * ---------------------------------------------------------------------------
     * [ ] 5.1 Store NVRAM in RP2350 flash
     *         - Create flash partition at 0x10180000 (64KB)
     *         - Flash cyfmac55500-sdio.txt via picotool
     *
     * [ ] 5.2 Calculate NVRAM location in chip RAM
     *         - nvram_addr = ram_base + ram_size - nvram_len
     *         - Round down to 4-byte boundary
     *
     * [ ] 5.3 Download NVRAM
     *         - Same process as firmware, smaller size (~12KB)
     *         - Write nvram_len to last 4 bytes (for firmware to find it)
     *
     * PHASE 6: Firmware Startup
     * ---------------------------------------------------------------------------
     * [ ] 6.1 Write reset vector (for older chips, may not be needed for CYW55500)
     *         - backplane_write32(0x18000044, ram_base)  // if required
     *
     * [ ] 6.2 Release ARM core from reset
     *         - Find ARM core base via core enumeration
     *         - Write to reset control register
     *         - Or use CHIPCLKCSR HT request method
     *
     * [ ] 6.3 Wait for firmware ready
     *         - Poll mailbox register for HMB_DATA_FWREADY (0x08)
     *         - Mailbox at SDIO core + 0x40 (tohostmailbox)
     *         - Timeout: 2-5 seconds
     *
     * PHASE 7: Data Path Setup
     * ---------------------------------------------------------------------------
     * [ ] 7.1 Enable Function 2
     *         - CMD52 write: F0 reg 0x02 |= 0x04  // IOE |= F2
     *         - Poll: F0 reg 0x03 until bit 2 = 1  // IOR & F2
     *
     * [ ] 7.2 Set F2 block size
     *         - CMD52 write: F0 reg 0x110 = 512 & 0xFF   // F2 block size low
     *         - CMD52 write: F0 reg 0x111 = 512 >> 8     // F2 block size high
     *
     * [ ] 7.3 Enable interrupts (optional for polling mode)
     *         - CMD52 write: F0 reg 0x04 = 0x07  // IEN: master + F1 + F2
     *
     * PHASE 8: SDPCM Protocol
     * ---------------------------------------------------------------------------
     * [ ] 8.1 Implement SDPCM TX frame
     *         - Header (12 bytes): len, ~len, seq, channel, next_len,
     *           data_offset, flow_ctrl, max_seq, reserved[2]
     *         - Channels: 0=control, 1=event, 2=data
     *         - Send via CMD53 to F2
     *
     * [ ] 8.2 Implement SDPCM RX frame
     *         - Check F2 data available (interrupt or poll)
     *         - CMD53 read from F2
     *         - Parse SDPCM header, extract payload
     *
     * [ ] 8.3 Implement ioctl/iovar via SDPCM
     *         - BCDC header (16 bytes) + SDPCM header
     *         - Send on channel 0 (control)
     *         - Wait for response
     *
     * PHASE 9: WiFi Operations
     * ---------------------------------------------------------------------------
     * [ ] 9.1 Get MAC address
     *         - iovar_get("cur_etheraddr", mac, 6)
     *
     * [ ] 9.2 Bring interface up
     *         - ioctl(WLC_UP)
     *
     * [ ] 9.3 Scan for networks
     *         - ioctl(WLC_SCAN) with scan params
     *         - Wait for scan complete event
     *         - ioctl(WLC_SCAN_RESULTS) to get results
     *
     * [ ] 9.4 Connect to AP
     *         - iovar_set("wsec", WPA2_PSK)
     *         - iovar_set("wpa_auth", WPA2_AUTH)
     *         - ioctl(WLC_SET_WSEC_PMK, passphrase)
     *         - ioctl(WLC_SET_SSID, ssid)
     *         - Wait for link event
     *
     * ==========================================================================
     */

    LOG_INF("========================================");
    LOG_INF("SDIO Test Complete!");
    LOG_INF("========================================");

done:
    while (1) {
        k_msleep(1000);
    }

    return 0;
}
