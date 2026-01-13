#ifndef SDIO_UTILS_H
#define SDIO_UTILS_H

#include "sdio_hal.h"

// R1 Response flags (Card Status)
#define R1_OUT_OF_RANGE         (1 << 31)
#define R1_ADDRESS_ERROR        (1 << 30)
#define R1_BLOCK_LEN_ERROR      (1 << 29)
#define R1_ERASE_SEQ_ERROR      (1 << 28)
#define R1_ERASE_PARAM          (1 << 27)
#define R1_WP_VIOLATION         (1 << 26)
#define R1_CARD_IS_LOCKED       (1 << 25)
#define R1_LOCK_UNLOCK_FAILED   (1 << 24)
#define R1_COM_CRC_ERROR        (1 << 23)
#define R1_ILLEGAL_COMMAND      (1 << 22)
#define R1_CARD_ECC_FAILED      (1 << 21)
#define R1_CC_ERROR             (1 << 20)
#define R1_ERROR                (1 << 19)
#define R1_CSD_OVERWRITE        (1 << 16)
#define R1_WP_ERASE_SKIP        (1 << 15)
#define R1_CARD_ECC_DISABLED    (1 << 14)
#define R1_ERASE_RESET          (1 << 13)
#define R1_CURRENT_STATE_MASK   (0x0F << 9)
#define R1_READY_FOR_DATA       (1 << 8)
#define R1_APP_CMD              (1 << 5)
#define R1_AKE_SEQ_ERROR        (1 << 3)

// R5 Response flags (SDIO)
#define R5_COM_CRC_ERROR        (1 << 15)
#define R5_ILLEGAL_COMMAND      (1 << 14)
#define R5_IO_CURRENT_STATE_MASK (0x03 << 12)
#define R5_ERROR                (1 << 11)
#define R5_FUNCTION_NUMBER      (1 << 9)
#define R5_OUT_OF_RANGE         (1 << 8)

// CCCR (Card Common Control Registers) addresses
#define CCCR_SDIO_REVISION      0x00
#define CCCR_SD_SPEC_REVISION   0x01
#define CCCR_IO_ENABLE          0x02
#define CCCR_IO_READY           0x03
#define CCCR_INT_ENABLE         0x04
#define CCCR_INT_PENDING        0x05
#define CCCR_IO_ABORT           0x06
#define CCCR_BUS_CONTROL        0x07
#define CCCR_CARD_CAPABILITY    0x08
#define CCCR_COMMON_CIS_POINTER 0x09  // 3 bytes: 0x09-0x0B
#define CCCR_BUS_SUSPEND        0x0C
#define CCCR_FUNCTION_SELECT    0x0D
#define CCCR_EXEC_FLAGS         0x0E
#define CCCR_READY_FLAGS        0x0F
#define CCCR_FN0_BLOCK_SIZE     0x10  // 2 bytes: 0x10-0x11
#define CCCR_POWER_CONTROL      0x12
#define CCCR_HIGH_SPEED         0x13

// Bus width values for CCCR_BUS_CONTROL
#define BUS_WIDTH_1BIT          0x00
#define BUS_WIDTH_4BIT          0x02
#define BUS_WIDTH_8BIT          0x03

// Helper functions for CMD52 (Direct I/O)
static inline uint32_t sdio_cmd52_arg(bool write, uint8_t func,
                                       uint32_t addr, uint8_t data) {
    return ((write ? 1 : 0) << 31) |
           ((func & 0x7) << 28) |
           (0 << 27) |  // RAW flag
           ((addr & 0x1FFFF) << 9) |
           (data & 0xFF);
}

static inline uint8_t sdio_cmd52_get_data(uint32_t response) {
    return response & 0xFF;
}

static inline uint16_t sdio_cmd52_get_flags(uint32_t response) {
    return (response >> 8) & 0xFFFF;
}

// Helper functions for CMD53 (Extended I/O)
static inline uint32_t sdio_cmd53_arg(bool write, uint8_t func, bool block_mode,
                                       bool op_code, uint32_t addr, uint16_t count) {
    return ((write ? 1 : 0) << 31) |
           ((func & 0x7) << 28) |
           ((block_mode ? 1 : 0) << 27) |
           ((op_code ? 1 : 0) << 26) |  // 0=fixed, 1=incrementing
           ((addr & 0x1FFFF) << 9) |
           (count & 0x1FF);
}

// CMD5 (IO_SEND_OP_COND) response parsing
typedef struct {
    bool card_ready;
    uint8_t num_functions;
    bool memory_present;
    uint32_t io_ocr;  // Operating Conditions Register
} sdio_cmd5_response_t;

static inline void sdio_parse_cmd5_response(uint32_t response,
                                             sdio_cmd5_response_t *parsed) {
    if (parsed) {
        parsed->card_ready = (response & 0x80000000) != 0;
        parsed->num_functions = (response >> 28) & 0x07;
        parsed->memory_present = (response & 0x08000000) != 0;
        parsed->io_ocr = response & 0x00FFFFFF;
    }
}

// Direct I/O register read/write helpers
static inline sdio_status_t sdio_read_reg(uint8_t func, uint32_t addr,
                                           uint8_t *value) {
    sdio_response_t resp;
    uint32_t arg = sdio_cmd52_arg(false, func, addr, 0);
    sdio_status_t status = sdio_send_cmd(SD_CMD52_IO_RW_DIRECT, arg, &resp);
    if (status == SDIO_OK && value) {
        *value = sdio_cmd52_get_data(resp.arg[0]);
    }
    return status;
}

static inline sdio_status_t sdio_write_reg(uint8_t func, uint32_t addr,
                                            uint8_t value) {
    sdio_response_t resp;
    uint32_t arg = sdio_cmd52_arg(true, func, addr, value);
    return sdio_send_cmd(SD_CMD52_IO_RW_DIRECT, arg, &resp);
}

// CCCR register access helpers
static inline sdio_status_t sdio_read_cccr(uint8_t reg_addr, uint8_t *value) {
    return sdio_read_reg(0, reg_addr, value);
}

static inline sdio_status_t sdio_write_cccr(uint8_t reg_addr, uint8_t value) {
    return sdio_write_reg(0, reg_addr, value);
}

// Set bus width (1-bit or 4-bit)
static inline sdio_status_t sdio_set_bus_width(uint8_t width) {
    uint8_t bus_ctrl;
    sdio_status_t status = sdio_read_cccr(CCCR_BUS_CONTROL, &bus_ctrl);
    if (status != SDIO_OK) return status;

    bus_ctrl = (bus_ctrl & ~0x03) | (width & 0x03);
    return sdio_write_cccr(CCCR_BUS_CONTROL, bus_ctrl);
}

// Enable/disable I/O function
static inline sdio_status_t sdio_enable_function(uint8_t func, bool enable) {
    uint8_t io_enable;
    sdio_status_t status = sdio_read_cccr(CCCR_IO_ENABLE, &io_enable);
    if (status != SDIO_OK) return status;

    if (enable) {
        io_enable |= (1 << func);
    } else {
        io_enable &= ~(1 << func);
    }
    return sdio_write_cccr(CCCR_IO_ENABLE, io_enable);
}

// Check if I/O function is ready
static inline sdio_status_t sdio_is_function_ready(uint8_t func, bool *ready) {
    uint8_t io_ready;
    sdio_status_t status = sdio_read_cccr(CCCR_IO_READY, &io_ready);
    if (status == SDIO_OK && ready) {
        *ready = (io_ready & (1 << func)) != 0;
    }
    return status;
}

// Enable/disable interrupt for function
static inline sdio_status_t sdio_enable_interrupt(uint8_t func, bool enable) {
    uint8_t int_enable;
    sdio_status_t status = sdio_read_cccr(CCCR_INT_ENABLE, &int_enable);
    if (status != SDIO_OK) return status;

    if (enable) {
        int_enable |= (1 << func);
        int_enable |= (1 << 0);  // Master interrupt enable
    } else {
        int_enable &= ~(1 << func);
    }
    return sdio_write_cccr(CCCR_INT_ENABLE, int_enable);
}

// Set block size for function
static inline sdio_status_t sdio_set_block_size(uint8_t func, uint16_t block_size) {
    uint32_t addr = (func == 0) ? CCCR_FN0_BLOCK_SIZE : (0x100 * func + 0x10);
    sdio_status_t status;

    // Write low byte
    status = sdio_write_reg(0, addr, block_size & 0xFF);
    if (status != SDIO_OK) return status;

    // Write high byte
    return sdio_write_reg(0, addr + 1, (block_size >> 8) & 0xFF);
}

#endif // SDIO_UTILS_H
