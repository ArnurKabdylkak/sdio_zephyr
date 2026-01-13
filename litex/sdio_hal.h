#ifndef SDIO_HAL_H
#define SDIO_HAL_H

#include <stdint.h>
#include <stdbool.h>

// Base address from SoC configuration (0x80000000)
#ifndef SDIO_BASE
#define SDIO_BASE 0x80000000
#endif

// Register offsets from WishboneController.sv
#define SDIO_MAIN_CLOCK_FREQ_OFFSET     0x0000
#define SDIO_SD_CLOCK_FREQ_OFFSET       0x1000
#define SDIO_CMD_INDEX_OFFSET           0x2000
#define SDIO_CMD_ARGUMENT_OFFSET        0x3000
#define SDIO_DATA_BUFFER_OFFSET         0x4000
#define SDIO_SEND_CMD_OP_OFFSET         0x5000
#define SDIO_SEND_CMD_READ_DATA_OP_OFFSET   0x6000
#define SDIO_SEND_CMD_SEND_DATA_OP_OFFSET   0x7000
#define SDIO_READ_DATA_OP_OFFSET        0x8000
#define SDIO_SEND_DATA_OP_OFFSET        0x9000
#define SDIO_CMD_BUSY_OFFSET            0xA000
#define SDIO_DATA_BUSY_OFFSET           0xB000
#define SDIO_CMD_STATUS_OFFSET          0xC000
#define SDIO_DATA_STATUS_OFFSET         0xD000
#define SDIO_DATA_LENGTH_OFFSET         0xE000

// Data buffer size (512 x 32-bit words = 2048 bytes)
#define SDIO_DATA_BUFFER_SIZE_WORDS     512
#define SDIO_DATA_BUFFER_SIZE_BYTES     2048

// Command status bits
#define SDIO_CMD_STATUS_TIMEOUT         (1 << 0)
#define SDIO_CMD_STATUS_INDEX_MASK      0x7E  // bits [6:1]
#define SDIO_CMD_STATUS_INDEX_SHIFT     1

// Data status bits
#define SDIO_DATA_STATUS_ERROR          (1 << 0)
#define SDIO_DATA_STATUS_TIMEOUT        (1 << 1)

// SD Command indices (from SD spec and HDL)
#define SD_CMD0_GO_IDLE_STATE           0
#define SD_CMD2_ALL_SEND_CID            2
#define SD_CMD3_SEND_RELATIVE_ADDR      3
#define SD_CMD5_IO_SEND_OP_COND         5
#define SD_CMD7_SELECT_CARD             7
#define SD_CMD8_SEND_IF_COND            8
#define SD_CMD9_SEND_CSD                9
#define SD_CMD10_SEND_CID               10
#define SD_CMD52_IO_RW_DIRECT           52
#define SD_CMD53_IO_RW_EXTENDED         53
#define SD_ACMD41_SD_SEND_OP_COND       41
#define SD_CMD55_APP_CMD                55

// Response types
typedef enum {
    SDIO_RESP_NONE,
    SDIO_RESP_SHORT,  // 48-bit (R1, R3, R4, R5, R6, R7)
    SDIO_RESP_LONG    // 136-bit (R2)
} sdio_response_type_t;

// Command response structure
typedef struct {
    uint8_t index;
    uint32_t arg[4];  // 4 x 32-bit for long responses
    bool timeout;
} sdio_response_t;

// HAL status codes
typedef enum {
    SDIO_OK = 0,
    SDIO_ERROR_TIMEOUT,
    SDIO_ERROR_CRC,
    SDIO_ERROR_BUSY,
    SDIO_ERROR_INVALID_PARAM
} sdio_status_t;

// Core functions
void sdio_init(uint32_t main_clk_freq, uint32_t sd_clk_freq);
void sdio_set_clock_freq(uint32_t sd_clk_freq);
uint32_t sdio_get_clock_freq(void);

// Command operations
sdio_status_t sdio_send_cmd(uint8_t cmd_index, uint32_t arg, sdio_response_t *resp);
sdio_status_t sdio_send_cmd_with_data_read(uint8_t cmd_index, uint32_t arg,
                                            uint32_t *data_buf, uint16_t data_len,
                                            sdio_response_t *resp);
sdio_status_t sdio_send_cmd_with_data_write(uint8_t cmd_index, uint32_t arg,
                                             const uint32_t *data_buf, uint16_t data_len,
                                             sdio_response_t *resp);

// Data-only operations
sdio_status_t sdio_read_data(uint32_t *data_buf, uint16_t data_len);
sdio_status_t sdio_write_data(const uint32_t *data_buf, uint16_t data_len);

// Status check functions
bool sdio_is_cmd_busy(void);
bool sdio_is_data_busy(void);
void sdio_wait_cmd_ready(void);
void sdio_wait_data_ready(void);

// Low-level register access
static inline void sdio_write_reg(uint32_t offset, uint32_t value) {
    *((volatile uint32_t*)(SDIO_BASE + offset)) = value;
}

static inline uint32_t sdio_read_reg(uint32_t offset) {
    return *((volatile uint32_t*)(SDIO_BASE + offset));
}

#endif // SDIO_HAL_H
