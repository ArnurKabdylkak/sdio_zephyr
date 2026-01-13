/**
 * SDIO Host Controller - LiteX/RISC-V Platform Implementation
 *
 * TODO: Complete implementation based on your specific SDIO controller
 */

#include "baremetal.h"
#include "sdio_litex.h"

/*============================================================================
 * Private State
 *============================================================================*/

static struct {
    bool initialized;
    uint16_t rca;           /* Relative Card Address */
    uint16_t block_size[8]; /* Block size per function */
} sdio_state;

/*============================================================================
 * Low-level Register Access
 *============================================================================*/

static inline void sdio_write_reg(uint32_t reg, uint32_t val)
{
    REG32(reg) = val;
}

static inline uint32_t sdio_read_reg(uint32_t reg)
{
    return REG32(reg);
}

/* Data buffer access */
static inline void sdio_write_data_buffer(uint32_t index, uint32_t val)
{
    ((volatile uint32_t *)SDIO_REG_DATA_BUFFER)[index] = val;
}

static inline uint32_t sdio_read_data_buffer(uint32_t index)
{
    return ((volatile uint32_t *)SDIO_REG_DATA_BUFFER)[index];
}

/*============================================================================
 * Command Execution
 *============================================================================*/

/**
 * Wait for command completion
 */
static int wait_cmd_complete(uint32_t timeout_ms)
{
    while (timeout_ms--) {
        if (!sdio_read_reg(SDIO_REG_CMD_BUSY)) {
            uint32_t status = sdio_read_reg(SDIO_REG_CMD_STATUS);
            if (status & SDIO_CMD_STATUS_TIMEOUT) {
                return -2;
            }
            return 0;
        }
        litex_delay_ms(1);
    }
    return -2; /* Timeout */
}


/**
 * Send SDIO command (command only, no data)
 */
static int send_command(uint8_t cmd, uint32_t arg, uint8_t rsp_type, uint32_t *response)
{
    (void)rsp_type; /* Response type determined by hardware */

    /* Set command index and argument */
    sdio_write_reg(SDIO_REG_CMD_INDEX, cmd);
    sdio_write_reg(SDIO_REG_CMD_ARGUMENT, arg);

    /* Trigger command - wait for operation register to return 0 */
    while (sdio_read_reg(SDIO_REG_SEND_CMD) != 0);

    /* Wait for command completion */
    int ret = wait_cmd_complete(100);
    if (ret != 0) {
        return ret;
    }

    /* Read response if needed */
    if (response) {
        *response = sdio_read_reg(SDIO_REG_CMD_ARGUMENT);
    }

    return 0;
}

/**
 * Send command and read data
 */
static int send_command_read_data(uint8_t cmd, uint32_t arg, uint32_t *response)
{
    /* Set command index and argument */
    sdio_write_reg(SDIO_REG_CMD_INDEX, cmd);
    sdio_write_reg(SDIO_REG_CMD_ARGUMENT, arg);

    /* Trigger command with data read */
    while (sdio_read_reg(SDIO_REG_SEND_CMD_READ_DATA) != 0);

    /* Wait for command and data completion */
    while (sdio_read_reg(SDIO_REG_CMD_BUSY) || sdio_read_reg(SDIO_REG_DATA_BUSY)) {
        litex_delay_us(10);
    }

    /* Check command status */
    uint32_t cmd_status = sdio_read_reg(SDIO_REG_CMD_STATUS);
    if (cmd_status & SDIO_CMD_STATUS_TIMEOUT) {
        return -2;
    }

    /* Check data status */
    uint32_t data_status = sdio_read_reg(SDIO_REG_DATA_STATUS);
    if (data_status & (SDIO_DATA_STATUS_CRC_ERROR | SDIO_DATA_STATUS_TIMEOUT)) {
        return -1;
    }

    if (response) {
        *response = sdio_read_reg(SDIO_REG_CMD_ARGUMENT);
    }

    return 0;
}

/**
 * Send command and write data
 */
static int send_command_write_data(uint8_t cmd, uint32_t arg, uint32_t *response)
{
    /* Set command index and argument */
    sdio_write_reg(SDIO_REG_CMD_INDEX, cmd);
    sdio_write_reg(SDIO_REG_CMD_ARGUMENT, arg);

    /* Trigger command with data write */
    while (sdio_read_reg(SDIO_REG_SEND_CMD_SEND_DATA) != 0);

    /* Wait for command and data completion */
    while (sdio_read_reg(SDIO_REG_CMD_BUSY) || sdio_read_reg(SDIO_REG_DATA_BUSY)) {
        litex_delay_us(10);
    }

    /* Check command status */
    uint32_t cmd_status = sdio_read_reg(SDIO_REG_CMD_STATUS);
    if (cmd_status & SDIO_CMD_STATUS_TIMEOUT) {
        return -2;
    }

    /* Check data status */
    uint32_t data_status = sdio_read_reg(SDIO_REG_DATA_STATUS);
    if (data_status & (SDIO_DATA_STATUS_CRC_ERROR | SDIO_DATA_STATUS_TIMEOUT)) {
        return -1;
    }

    if (response) {
        *response = sdio_read_reg(SDIO_REG_CMD_ARGUMENT);
    }

    return 0;
}

/*============================================================================
 * SDIO Card Initialization
 *============================================================================*/

static int sdio_card_init(void)
{
    uint32_t response;
    int ret;
    int retry;

    /* Send CMD0 - Go Idle */
    ret = send_command(CMD0_GO_IDLE, 0, RSP_NONE, NULL);
    if (ret != 0) {
        return ret;
    }

    litex_delay_ms(10);

    /* Send CMD5 - IO_SEND_OP_COND to get OCR */
    /* First with arg=0 to query, then with voltage */
    ret = send_command(CMD5_IO_SEND_OP_COND, 0, RSP_R4, &response);
    if (ret != 0) {
        return ret;
    }

    /* Set voltage (3.3V) and wait for ready */
    uint32_t ocr = 0x00FF8000; /* 3.2-3.4V */
    retry = 100;

    while (retry--) {
        ret = send_command(CMD5_IO_SEND_OP_COND, ocr, RSP_R4, &response);
        if (ret != 0) {
            return ret;
        }

        if (response & 0x80000000) { /* Card ready bit */
            break;
        }
        litex_delay_ms(10);
    }

    if (retry <= 0) {
        return -2; /* Timeout */
    }

    /* Send CMD3 - Get RCA */
    ret = send_command(CMD3_SEND_RCA, 0, RSP_R6, &response);
    if (ret != 0) {
        return ret;
    }

    sdio_state.rca = (response >> 16) & 0xFFFF;

    /* Send CMD7 - Select Card */
    ret = send_command(CMD7_SELECT_CARD, sdio_state.rca << 16, RSP_R1, &response);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

/*============================================================================
 * CMD52 Implementation (IO_RW_DIRECT)
 *============================================================================*/

int litex_sdio_cmd52_read(uint8_t func, uint32_t addr, uint8_t *val)
{
    uint32_t arg;
    uint32_t response;
    int ret;

    /* Build CMD52 argument:
     * [31]    R/W flag: 0=read, 1=write
     * [30:28] Function number
     * [27]    RAW flag
     * [25:9]  Register address
     * [7:0]   Write data (ignored for read)
     */
    arg = ((func & 0x7) << 28) |
          ((addr & 0x1FFFF) << 9);

    ret = send_command(CMD52_IO_RW_DIRECT, arg, RSP_R5, &response);
    if (ret != 0) {
        return ret;
    }

    /* Check response flags */
    if (response & 0xCB00) { /* Error flags in R5 */
        return -1;
    }

    *val = response & 0xFF;
    return 0;
}

int litex_sdio_cmd52_write(uint8_t func, uint32_t addr, uint8_t val)
{
    uint32_t arg;
    uint32_t response;
    int ret;

    /* Build CMD52 argument for write */
    arg = (1 << 31) |                   /* Write flag */
          ((func & 0x7) << 28) |
          ((addr & 0x1FFFF) << 9) |
          (val & 0xFF);

    ret = send_command(CMD52_IO_RW_DIRECT, arg, RSP_R5, &response);
    if (ret != 0) {
        return ret;
    }

    /* Check response flags */
    if (response & 0xCB00) {
        return -1;
    }

    return 0;
}

/*============================================================================
 * CMD53 Implementation (IO_RW_EXTENDED)
 *============================================================================*/

int litex_sdio_cmd53_read(uint8_t func, uint32_t addr, uint8_t *data,
                          uint32_t len, bool incr_addr)
{
    uint32_t arg;
    uint32_t response;
    int ret;

    /* Set data length */
    sdio_write_reg(SDIO_REG_DATA_LENGTH, len);

    /* Build CMD53 argument:
     * [31]    R/W flag: 0=read
     * [30:28] Function number
     * [27]    Block mode: 0=byte, 1=block
     * [26]    OP code: 0=fixed addr, 1=incrementing addr
     * [25:9]  Register address
     * [8:0]   Byte/Block count
     */
    arg = ((func & 0x7) << 28) |
          (incr_addr ? (1 << 26) : 0) |
          ((addr & 0x1FFFF) << 9) |
          (len & 0x1FF);

    /* Send command and read data */
    ret = send_command_read_data(CMD53_IO_RW_EXTENDED, arg, &response);
    if (ret != 0) {
        return ret;
    }

    /* Check response flags */
    if (response & 0xCB00) {
        return -1;
    }

    /* Read data from buffer */
    uint32_t *data32 = (uint32_t *)data;
    uint32_t words = len / 4;

    for (uint32_t i = 0; i < words; i++) {
        data32[i] = sdio_read_data_buffer(i);
    }

    /* Handle remaining bytes */
    uint32_t remaining = len % 4;
    if (remaining > 0) {
        uint32_t last = sdio_read_data_buffer(words);
        uint8_t *p = data + (words * 4);
        for (uint32_t i = 0; i < remaining; i++) {
            p[i] = (last >> (i * 8)) & 0xFF;
        }
    }

    return 0;
}

int litex_sdio_cmd53_write(uint8_t func, uint32_t addr, const uint8_t *data,
                           uint32_t len, bool incr_addr)
{
    uint32_t arg;
    uint32_t response;
    int ret;

    /* Set data length */
    sdio_write_reg(SDIO_REG_DATA_LENGTH, len);

    /* Write data to buffer first */
    const uint32_t *data32 = (const uint32_t *)data;
    uint32_t words = len / 4;

    for (uint32_t i = 0; i < words; i++) {
        sdio_write_data_buffer(i, data32[i]);
    }

    /* Handle remaining bytes */
    uint32_t remaining = len % 4;
    if (remaining > 0) {
        uint32_t last = 0;
        const uint8_t *p = data + (words * 4);
        for (uint32_t i = 0; i < remaining; i++) {
            last |= ((uint32_t)p[i]) << (i * 8);
        }
        sdio_write_data_buffer(words, last);
    }

    /* Build CMD53 argument for write */
    arg = (1 << 31) |                   /* Write flag */
          ((func & 0x7) << 28) |
          (incr_addr ? (1 << 26) : 0) |
          ((addr & 0x1FFFF) << 9) |
          (len & 0x1FF);

    /* Send command and write data */
    ret = send_command_write_data(CMD53_IO_RW_EXTENDED, arg, &response);
    if (ret != 0) {
        return ret;
    }

    /* Check response flags */
    if (response & 0xCB00) {
        return -1;
    }

    return 0;
}

/*============================================================================
 * Function Management
 *============================================================================*/

int litex_sdio_set_block_size(uint8_t func, uint16_t block_size)
{
    int ret;

    if (func > 7) {
        return -1;
    }

    /* Write block size to FBR */
    uint32_t addr = 0x100 * func + 0x10; /* FBR block size register */

    ret = litex_sdio_cmd52_write(0, addr, block_size & 0xFF);
    if (ret != 0) return ret;

    ret = litex_sdio_cmd52_write(0, addr + 1, (block_size >> 8) & 0xFF);
    if (ret != 0) return ret;

    sdio_state.block_size[func] = block_size;
    return 0;
}

int litex_sdio_enable_func(uint8_t func, bool enable)
{
    uint8_t val;
    int ret;

    /* Read current IO Enable register */
    ret = litex_sdio_cmd52_read(0, 0x02, &val); /* CCCR IO Enable */
    if (ret != 0) return ret;

    if (enable) {
        val |= (1 << func);
    } else {
        val &= ~(1 << func);
    }

    ret = litex_sdio_cmd52_write(0, 0x02, val);
    return ret;
}

int litex_sdio_enable_irq(bool enable)
{
    (void)enable;
    /* TODO: Implement when interrupt support is added to hardware */
    return 0;
}

bool litex_sdio_irq_pending(void)
{
    /* TODO: Implement when interrupt support is added to hardware */
    return false;
}

/*============================================================================
 * Initialization
 *============================================================================*/

int litex_sdio_init(void)
{
    int ret;

    memset(&sdio_state, 0, sizeof(sdio_state));

    /* Read clock frequencies for debugging */
    uint32_t main_clk = sdio_read_reg(SDIO_REG_MAIN_CLK_FREQ);
    uint32_t sdio_clk = sdio_read_reg(SDIO_REG_SDIO_CLK_FREQ);
    (void)main_clk;
    (void)sdio_clk;

    litex_delay_ms(10);

    /* Initialize card */
    ret = sdio_card_init();
    if (ret != 0) {
        return ret;
    }

    /* Enable 4-bit mode if supported */
    uint8_t bus_width;
    ret = litex_sdio_cmd52_read(0, 0x07, &bus_width); /* CCCR Bus Interface Control */
    if (ret == 0) {
        bus_width = (bus_width & ~0x03) | 0x02; /* Set 4-bit mode */
        litex_sdio_cmd52_write(0, 0x07, bus_width);
    }

    sdio_state.initialized = true;
    return 0;
}

void litex_sdio_deinit(void)
{
    sdio_state.initialized = false;
}

/*============================================================================
 * Operations Structure
 *============================================================================*/

static const sdio_host_ops_t litex_sdio_ops = {
    .init = litex_sdio_init,
    .deinit = litex_sdio_deinit,
    .cmd52_read = litex_sdio_cmd52_read,
    .cmd52_write = litex_sdio_cmd52_write,
    .cmd53_read = litex_sdio_cmd53_read,
    .cmd53_write = litex_sdio_cmd53_write,
    .set_block_size = litex_sdio_set_block_size,
    .enable_func = litex_sdio_enable_func,
    .enable_irq = litex_sdio_enable_irq,
    .irq_pending = litex_sdio_irq_pending,
    .delay_us = litex_delay_us,
    .delay_ms = litex_delay_ms,
};

const sdio_host_ops_t *litex_get_sdio_ops(void)
{
    return &litex_sdio_ops;
}
