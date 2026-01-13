/**
 * SDIO HAL for RP2350 (Raspberry Pi Pico 2)
 * Bit-bang implementation using Zephyr GPIO
 *
 * Pin mapping for Quectel FCS96xN:
 *   GP18 - SDIO_CLK
 *   GP19 - SDIO_CMD
 *   GP20 - SDIO_D0
 *   GP21 - SDIO_D1
 *   GP22 - SDIO_D2
 *   GP26 - SDIO_D3
 *   GP27 - WL_REG_ON
 *   GP28 - WL_HOST_WAKE (optional)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "cyw55500_sdio.h"

LOG_MODULE_REGISTER(sdio_rp2350, CONFIG_LOG_DEFAULT_LEVEL);

/*============================================================================
 * GPIO Pin Definitions
 *============================================================================*/

#define GPIO0_NODE DT_NODELABEL(gpio0)

#define PIN_CLK      18
#define PIN_CMD      19
#define PIN_D0       20
#define PIN_D1       21
#define PIN_D2       22
#define PIN_D3       26
#define PIN_REG_ON   27
#define PIN_HOST_WAKE 28

static const struct device *gpio_dev;

/*============================================================================
 * SDIO State
 *============================================================================*/

static uint16_t rca = 0;  /* Relative Card Address */
static bool card_initialized = false;
static uint16_t func_block_size[8] = {0};

/*============================================================================
 * Low-level GPIO helpers
 *============================================================================*/

static inline void clk_high(void)
{
    gpio_pin_set(gpio_dev, PIN_CLK, 1);
}

static inline void clk_low(void)
{
    gpio_pin_set(gpio_dev, PIN_CLK, 0);
}

static inline void cmd_high(void)
{
    gpio_pin_set(gpio_dev, PIN_CMD, 1);
}

static inline void cmd_low(void)
{
    gpio_pin_set(gpio_dev, PIN_CMD, 0);
}

static inline void cmd_output(void)
{
    gpio_pin_configure(gpio_dev, PIN_CMD, GPIO_OUTPUT);
}

static inline void cmd_input(void)
{
    gpio_pin_configure(gpio_dev, PIN_CMD, GPIO_INPUT);
}

static inline int cmd_read(void)
{
    return gpio_pin_get(gpio_dev, PIN_CMD);
}

static inline void d0_output(void)
{
    gpio_pin_configure(gpio_dev, PIN_D0, GPIO_OUTPUT);
}

static inline void d0_input(void)
{
    gpio_pin_configure(gpio_dev, PIN_D0, GPIO_INPUT);
}

static inline int d0_read(void)
{
    return gpio_pin_get(gpio_dev, PIN_D0);
}

static inline void clock_cycle(void)
{
    clk_high();
    k_busy_wait(1);
    clk_low();
    k_busy_wait(1);
}

/*============================================================================
 * CRC7 for commands
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
 * Send/Receive bits
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
        clock_cycle();
    }
}

static uint64_t receive_bits(int bits)
{
    uint64_t data = 0;
    cmd_input();
    for (int i = 0; i < bits; i++) {
        clk_high();
        k_busy_wait(1);
        data <<= 1;
        if (cmd_read()) {
            data |= 1;
        }
        clk_low();
        k_busy_wait(1);
    }
    return data;
}

/*============================================================================
 * SDIO Command Layer
 *============================================================================*/

static int wait_cmd_response(void)
{
    cmd_input();

    /* Wait for start bit (0) */
    for (int i = 0; i < 1000; i++) {
        clk_high();
        k_busy_wait(1);
        int bit = cmd_read();
        clk_low();
        k_busy_wait(1);

        if (bit == 0) {
            return 0;
        }
    }
    return -1;  /* Timeout */
}

static int send_command(uint8_t cmd, uint32_t arg, uint32_t *response)
{
    uint8_t buf[5];
    buf[0] = 0x40 | cmd;
    buf[1] = (arg >> 24) & 0xFF;
    buf[2] = (arg >> 16) & 0xFF;
    buf[3] = (arg >> 8) & 0xFF;
    buf[4] = arg & 0xFF;

    uint8_t crc = crc7(buf, 5);

    /* Send command: start(1) + tx(1) + cmd(6) + arg(32) + crc(7) + stop(1) = 48 bits */
    uint64_t frame = 0;
    frame |= (1ULL << 47);                    /* Start bit = 1 */
    frame |= (1ULL << 46);                    /* Transmission bit = 1 (host) */
    frame |= ((uint64_t)cmd << 40);           /* Command index */
    frame |= ((uint64_t)arg << 8);            /* Argument */
    frame |= crc;                             /* CRC7 + stop bit */

    send_bits(frame, 48);

    /* Wait for response */
    if (wait_cmd_response() < 0) {
        LOG_ERR("CMD%d: no response", cmd);
        return -1;
    }

    /* Read R5 response: tx(1) + cmd(6) + stuff(8) + response(8) + stuff(8) + crc(7) = 38 bits remaining */
    /* We already consumed start bit */
    uint64_t resp = receive_bits(39);

    if (response) {
        *response = (resp >> 7) & 0xFFFFFFFF;
    }

    /* Clock out 8 cycles */
    for (int i = 0; i < 8; i++) {
        clock_cycle();
    }

    return 0;
}

/*============================================================================
 * CMD52 - Single byte read/write
 *============================================================================*/

static int sdio_cmd52_read(uint8_t func, uint32_t addr, uint8_t *val)
{
    /* CMD52 argument: R/W(1) + func(3) + RAW(1) + stuff(1) + addr(17) + stuff(1) + data(8) */
    uint32_t arg = 0;
    arg |= (0 << 31);              /* Read */
    arg |= ((func & 0x7) << 28);   /* Function */
    arg |= (0 << 27);              /* RAW = 0 */
    arg |= ((addr & 0x1FFFF) << 9);/* Register address */
    arg |= 0;                      /* Data = 0 for read */

    uint32_t response;
    int ret = send_command(52, arg, &response);
    if (ret < 0) {
        return ret;
    }

    /* Check response flags */
    uint8_t flags = (response >> 8) & 0xFF;
    if (flags & 0xCB) {
        LOG_ERR("CMD52 read error: flags=0x%02x", flags);
        return -1;
    }

    *val = response & 0xFF;
    return 0;
}

static int sdio_cmd52_write(uint8_t func, uint32_t addr, uint8_t val)
{
    uint32_t arg = 0;
    arg |= (1 << 31);              /* Write */
    arg |= ((func & 0x7) << 28);   /* Function */
    arg |= (0 << 27);              /* RAW = 0 */
    arg |= ((addr & 0x1FFFF) << 9);/* Register address */
    arg |= val;                    /* Data */

    uint32_t response;
    int ret = send_command(52, arg, &response);
    if (ret < 0) {
        return ret;
    }

    uint8_t flags = (response >> 8) & 0xFF;
    if (flags & 0xCB) {
        LOG_ERR("CMD52 write error: flags=0x%02x", flags);
        return -1;
    }

    return 0;
}

/*============================================================================
 * CMD53 - Multi-byte read/write
 *============================================================================*/

static void send_data_byte(uint8_t val)
{
    d0_output();
    for (int i = 7; i >= 0; i--) {
        gpio_pin_set(gpio_dev, PIN_D0, (val >> i) & 1);
        clock_cycle();
    }
}

static uint8_t receive_data_byte(void)
{
    uint8_t val = 0;
    d0_input();
    for (int i = 0; i < 8; i++) {
        clk_high();
        k_busy_wait(1);
        val <<= 1;
        if (d0_read()) {
            val |= 1;
        }
        clk_low();
        k_busy_wait(1);
    }
    return val;
}

static int wait_data_start(void)
{
    d0_input();
    for (int i = 0; i < 10000; i++) {
        clk_high();
        k_busy_wait(1);
        int bit = d0_read();
        clk_low();
        k_busy_wait(1);
        if (bit == 0) {
            return 0;
        }
    }
    return -1;
}

static int sdio_cmd53_read(uint8_t func, uint32_t addr, uint8_t *data,
                           uint32_t len, bool incr_addr)
{
    /* Determine if block mode */
    bool block_mode = false;
    uint32_t count = len;

    if (func_block_size[func] > 0 && len >= func_block_size[func]) {
        block_mode = true;
        count = (len + func_block_size[func] - 1) / func_block_size[func];
    }

    /* CMD53 argument */
    uint32_t arg = 0;
    arg |= (0 << 31);                      /* Read */
    arg |= ((func & 0x7) << 28);           /* Function */
    arg |= (block_mode ? (1 << 27) : 0);   /* Block mode */
    arg |= (incr_addr ? (1 << 26) : 0);    /* OP code (incr addr) */
    arg |= ((addr & 0x1FFFF) << 9);        /* Address */
    arg |= (count & 0x1FF);                /* Byte/block count */

    uint32_t response;
    int ret = send_command(53, arg, &response);
    if (ret < 0) {
        return ret;
    }

    uint8_t flags = (response >> 8) & 0xFF;
    if (flags & 0xCB) {
        LOG_ERR("CMD53 read error: flags=0x%02x", flags);
        return -1;
    }

    /* Wait for data start token */
    if (wait_data_start() < 0) {
        LOG_ERR("CMD53 read: no data start");
        return -1;
    }

    /* Read data */
    for (uint32_t i = 0; i < len; i++) {
        data[i] = receive_data_byte();
    }

    /* Skip CRC16 (16 bits) */
    for (int i = 0; i < 16; i++) {
        clock_cycle();
    }

    return 0;
}

static int sdio_cmd53_write(uint8_t func, uint32_t addr, const uint8_t *data,
                            uint32_t len, bool incr_addr)
{
    bool block_mode = false;
    uint32_t count = len;

    if (func_block_size[func] > 0 && len >= func_block_size[func]) {
        block_mode = true;
        count = (len + func_block_size[func] - 1) / func_block_size[func];
    }

    uint32_t arg = 0;
    arg |= (1 << 31);                      /* Write */
    arg |= ((func & 0x7) << 28);
    arg |= (block_mode ? (1 << 27) : 0);
    arg |= (incr_addr ? (1 << 26) : 0);
    arg |= ((addr & 0x1FFFF) << 9);
    arg |= (count & 0x1FF);

    uint32_t response;
    int ret = send_command(53, arg, &response);
    if (ret < 0) {
        return ret;
    }

    uint8_t flags = (response >> 8) & 0xFF;
    if (flags & 0xCB) {
        LOG_ERR("CMD53 write error: flags=0x%02x", flags);
        return -1;
    }

    /* Send data start token (0) */
    d0_output();
    gpio_pin_set(gpio_dev, PIN_D0, 0);
    clock_cycle();

    /* Send data */
    for (uint32_t i = 0; i < len; i++) {
        send_data_byte(data[i]);
    }

    /* Send dummy CRC16 */
    for (int i = 0; i < 16; i++) {
        gpio_pin_set(gpio_dev, PIN_D0, 1);
        clock_cycle();
    }

    /* Wait for card response */
    d0_input();
    for (int i = 0; i < 100; i++) {
        clock_cycle();
        if (d0_read() == 1) {
            break;
        }
    }

    return 0;
}

/*============================================================================
 * Set Block Size
 *============================================================================*/

static int sdio_set_block_size(uint8_t func, uint16_t block_size)
{
    int ret;

    /* Write block size to CCCR FN_BLOCK_SIZE registers */
    uint32_t addr = 0x10 + (func * 0x100);

    ret = sdio_cmd52_write(0, addr, block_size & 0xFF);
    if (ret < 0) return ret;

    ret = sdio_cmd52_write(0, addr + 1, (block_size >> 8) & 0xFF);
    if (ret < 0) return ret;

    func_block_size[func] = block_size;
    LOG_INF("Set func%d block size to %d", func, block_size);

    return 0;
}

/*============================================================================
 * Enable Function
 *============================================================================*/

static int sdio_enable_func(uint8_t func, bool enable)
{
    uint8_t val;
    int ret;

    /* Read I/O Enable register */
    ret = sdio_cmd52_read(0, 0x02, &val);
    if (ret < 0) return ret;

    if (enable) {
        val |= (1 << func);
    } else {
        val &= ~(1 << func);
    }

    /* Write I/O Enable register */
    ret = sdio_cmd52_write(0, 0x02, val);
    if (ret < 0) return ret;

    if (enable) {
        /* Wait for function ready */
        for (int i = 0; i < 100; i++) {
            ret = sdio_cmd52_read(0, 0x03, &val);
            if (ret < 0) return ret;

            if (val & (1 << func)) {
                LOG_INF("Function %d enabled and ready", func);
                return 0;
            }
            k_msleep(10);
        }
        LOG_ERR("Function %d enable timeout", func);
        return -1;
    }

    return 0;
}

/*============================================================================
 * Enable/Check IRQ
 *============================================================================*/

static int sdio_enable_irq(bool enable)
{
    uint8_t val;
    int ret;

    ret = sdio_cmd52_read(0, 0x04, &val);
    if (ret < 0) return ret;

    if (enable) {
        val |= 0x03;  /* Master + Func1 */
    } else {
        val &= ~0x03;
    }

    return sdio_cmd52_write(0, 0x04, val);
}

static bool sdio_irq_pending(void)
{
    uint8_t val;
    if (sdio_cmd52_read(0, 0x05, &val) < 0) {
        return false;
    }
    return (val & 0x02) != 0;  /* Func1 interrupt */
}

/*============================================================================
 * Delay functions
 *============================================================================*/

static void sdio_delay_us(uint32_t us)
{
    k_busy_wait(us);
}

static void sdio_delay_ms(uint32_t ms)
{
    k_msleep(ms);
}

/*============================================================================
 * SDIO Card Initialization
 *============================================================================*/

static int sdio_card_init(void)
{
    uint32_t response;
    int ret;

    LOG_INF("Initializing SDIO card...");

    /* Send 74+ clock cycles with CMD high */
    cmd_output();
    cmd_high();
    for (int i = 0; i < 80; i++) {
        clock_cycle();
    }

    /* CMD0 - Go Idle (no response expected for SDIO) */
    send_command(0, 0, NULL);
    k_msleep(10);

    /* CMD5 - IO_SEND_OP_COND */
    ret = send_command(5, 0, &response);
    if (ret < 0) {
        LOG_ERR("CMD5 failed - no SDIO card?");
        return -1;
    }

    LOG_INF("CMD5 response: 0x%08x", response);

    /* Send CMD5 with voltage window until ready */
    uint32_t ocr = response & 0x00FFFFFF;
    for (int i = 0; i < 100; i++) {
        ret = send_command(5, ocr, &response);
        if (ret < 0) return ret;

        if (response & 0x80000000) {
            LOG_INF("SDIO card ready, OCR=0x%08x", response);
            break;
        }
        k_msleep(10);
    }

    if (!(response & 0x80000000)) {
        LOG_ERR("SDIO card not ready");
        return -1;
    }

    /* CMD3 - Send Relative Address */
    ret = send_command(3, 0, &response);
    if (ret < 0) {
        LOG_ERR("CMD3 failed");
        return -1;
    }

    rca = (response >> 16) & 0xFFFF;
    LOG_INF("Card RCA: 0x%04x", rca);

    /* CMD7 - Select Card */
    ret = send_command(7, rca << 16, &response);
    if (ret < 0) {
        LOG_ERR("CMD7 failed");
        return -1;
    }

    LOG_INF("Card selected");

    /* Read CCCR version */
    uint8_t cccr_ver;
    ret = sdio_cmd52_read(0, 0x00, &cccr_ver);
    if (ret < 0) {
        LOG_ERR("Failed to read CCCR");
        return -1;
    }
    LOG_INF("CCCR version: 0x%02x", cccr_ver);

    /* Enable high speed if supported */
    uint8_t bus_speed;
    ret = sdio_cmd52_read(0, 0x13, &bus_speed);
    if (ret == 0 && (bus_speed & 0x01)) {
        /* High speed supported */
        sdio_cmd52_write(0, 0x13, bus_speed | 0x02);
        LOG_INF("High speed enabled");
    }

    card_initialized = true;
    return 0;
}

/*============================================================================
 * HAL Init/Deinit
 *============================================================================*/

static int sdio_hal_init(void)
{
    LOG_INF("Initializing SDIO HAL for RP2350");

    gpio_dev = DEVICE_DT_GET(GPIO0_NODE);
    if (!device_is_ready(gpio_dev)) {
        LOG_ERR("GPIO device not ready");
        return -1;
    }

    /* Configure GPIO pins */
    gpio_pin_configure(gpio_dev, PIN_CLK, GPIO_OUTPUT);
    gpio_pin_configure(gpio_dev, PIN_CMD, GPIO_OUTPUT);
    gpio_pin_configure(gpio_dev, PIN_D0, GPIO_INPUT);
    gpio_pin_configure(gpio_dev, PIN_D1, GPIO_INPUT);
    gpio_pin_configure(gpio_dev, PIN_D2, GPIO_INPUT);
    gpio_pin_configure(gpio_dev, PIN_D3, GPIO_INPUT);
    gpio_pin_configure(gpio_dev, PIN_REG_ON, GPIO_OUTPUT);
    gpio_pin_configure(gpio_dev, PIN_HOST_WAKE, GPIO_INPUT);

    /* Power cycle WiFi chip */
    gpio_pin_set(gpio_dev, PIN_REG_ON, 0);
    k_msleep(100);
    gpio_pin_set(gpio_dev, PIN_REG_ON, 1);
    k_msleep(100);

    LOG_INF("WiFi chip powered on");

    /* Initialize clock low */
    clk_low();
    cmd_high();

    /* Initialize SDIO card */
    return sdio_card_init();
}

static void sdio_hal_deinit(void)
{
    LOG_INF("Deinitializing SDIO HAL");

    /* Power off WiFi chip */
    gpio_pin_set(gpio_dev, PIN_REG_ON, 0);

    card_initialized = false;
    rca = 0;
}

/*============================================================================
 * Public Interface
 *============================================================================*/

static const sdio_host_ops_t rp2350_sdio_ops = {
    .init = sdio_hal_init,
    .deinit = sdio_hal_deinit,
    .cmd52_read = sdio_cmd52_read,
    .cmd52_write = sdio_cmd52_write,
    .cmd53_read = sdio_cmd53_read,
    .cmd53_write = sdio_cmd53_write,
    .set_block_size = sdio_set_block_size,
    .enable_func = sdio_enable_func,
    .enable_irq = sdio_enable_irq,
    .irq_pending = sdio_irq_pending,
    .delay_us = sdio_delay_us,
    .delay_ms = sdio_delay_ms,
};

const sdio_host_ops_t *rp2350_get_sdio_ops(void)
{
    return &rp2350_sdio_ops;
}
