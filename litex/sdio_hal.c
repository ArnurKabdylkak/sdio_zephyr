#include "sdio_hal.h"
#include <string.h>

// Timeout for polling operations (iterations)
#define SDIO_POLL_TIMEOUT 100000

void sdio_init(uint32_t main_clk_freq, uint32_t sd_clk_freq) {
    // Set main clock frequency
    sdio_write_reg(SDIO_MAIN_CLOCK_FREQ_OFFSET, main_clk_freq);

    // Set SD clock frequency
    sdio_write_reg(SDIO_SD_CLOCK_FREQ_OFFSET, sd_clk_freq);

    // Set default data length (512 bytes for SD blocks)
    sdio_write_reg(SDIO_DATA_LENGTH_OFFSET, 512);
}

void sdio_set_clock_freq(uint32_t sd_clk_freq) {
    sdio_write_reg(SDIO_SD_CLOCK_FREQ_OFFSET, sd_clk_freq);
}

uint32_t sdio_get_clock_freq(void) {
    return sdio_read_reg(SDIO_SD_CLOCK_FREQ_OFFSET);
}

bool sdio_is_cmd_busy(void) {
    return (sdio_read_reg(SDIO_CMD_BUSY_OFFSET) & 0x1) != 0;
}

bool sdio_is_data_busy(void) {
    return (sdio_read_reg(SDIO_DATA_BUSY_OFFSET) & 0x1) != 0;
}

void sdio_wait_cmd_ready(void) {
    volatile uint32_t timeout = SDIO_POLL_TIMEOUT;
    while (sdio_is_cmd_busy() && timeout--);
}

void sdio_wait_data_ready(void) {
    volatile uint32_t timeout = SDIO_POLL_TIMEOUT;
    while (sdio_is_data_busy() && timeout--);
}

static sdio_status_t sdio_get_cmd_status(sdio_response_t *resp) {
    uint32_t status = sdio_read_reg(SDIO_CMD_STATUS_OFFSET);

    if (resp) {
        resp->timeout = (status & SDIO_CMD_STATUS_TIMEOUT) != 0;
        resp->index = (status & SDIO_CMD_STATUS_INDEX_MASK) >> SDIO_CMD_STATUS_INDEX_SHIFT;

        // Read response arguments (4 x 32-bit for long responses)
        resp->arg[0] = sdio_read_reg(SDIO_CMD_ARGUMENT_OFFSET + 0x0);
        resp->arg[1] = sdio_read_reg(SDIO_CMD_ARGUMENT_OFFSET + 0x4);
        resp->arg[2] = sdio_read_reg(SDIO_CMD_ARGUMENT_OFFSET + 0x8);
        resp->arg[3] = sdio_read_reg(SDIO_CMD_ARGUMENT_OFFSET + 0xC);
    }

    if (status & SDIO_CMD_STATUS_TIMEOUT) {
        return SDIO_ERROR_TIMEOUT;
    }

    return SDIO_OK;
}

static sdio_status_t sdio_get_data_status(void) {
    uint32_t status = sdio_read_reg(SDIO_DATA_STATUS_OFFSET);

    if (status & SDIO_DATA_STATUS_TIMEOUT) {
        return SDIO_ERROR_TIMEOUT;
    }

    if (status & SDIO_DATA_STATUS_ERROR) {
        return SDIO_ERROR_CRC;
    }

    return SDIO_OK;
}

sdio_status_t sdio_send_cmd(uint8_t cmd_index, uint32_t arg, sdio_response_t *resp) {
    // Check if command controller is busy
    if (sdio_is_cmd_busy()) {
        return SDIO_ERROR_BUSY;
    }

    // Set command index and argument
    sdio_write_reg(SDIO_CMD_INDEX_OFFSET, cmd_index);
    sdio_write_reg(SDIO_CMD_ARGUMENT_OFFSET, arg);

    // Trigger command send operation (read triggers the operation)
    sdio_read_reg(SDIO_SEND_CMD_OP_OFFSET);

    // Wait for command completion
    sdio_wait_cmd_ready();

    // Get command status and response
    return sdio_get_cmd_status(resp);
}

sdio_status_t sdio_send_cmd_with_data_read(uint8_t cmd_index, uint32_t arg,
                                            uint32_t *data_buf, uint16_t data_len,
                                            sdio_response_t *resp) {
    sdio_status_t status;

    // Validate parameters
    if (!data_buf || data_len == 0 || data_len > SDIO_DATA_BUFFER_SIZE_BYTES) {
        return SDIO_ERROR_INVALID_PARAM;
    }

    // Check if controllers are busy
    if (sdio_is_cmd_busy() || sdio_is_data_busy()) {
        return SDIO_ERROR_BUSY;
    }

    // Set data length
    sdio_write_reg(SDIO_DATA_LENGTH_OFFSET, data_len);

    // Set command index and argument
    sdio_write_reg(SDIO_CMD_INDEX_OFFSET, cmd_index);
    sdio_write_reg(SDIO_CMD_ARGUMENT_OFFSET, arg);

    // Trigger combined command + data read operation
    sdio_read_reg(SDIO_SEND_CMD_READ_DATA_OP_OFFSET);

    // Wait for both command and data completion
    sdio_wait_cmd_ready();
    sdio_wait_data_ready();

    // Check command status
    status = sdio_get_cmd_status(resp);
    if (status != SDIO_OK) {
        return status;
    }

    // Check data status
    status = sdio_get_data_status();
    if (status != SDIO_OK) {
        return status;
    }

    // Read data from buffer
    uint16_t word_count = (data_len + 3) / 4;  // Round up to word count
    for (uint16_t i = 0; i < word_count; i++) {
        data_buf[i] = sdio_read_reg(SDIO_DATA_BUFFER_OFFSET + (i * 4));
    }

    return SDIO_OK;
}

sdio_status_t sdio_send_cmd_with_data_write(uint8_t cmd_index, uint32_t arg,
                                             const uint32_t *data_buf, uint16_t data_len,
                                             sdio_response_t *resp) {
    sdio_status_t status;

    // Validate parameters
    if (!data_buf || data_len == 0 || data_len > SDIO_DATA_BUFFER_SIZE_BYTES) {
        return SDIO_ERROR_INVALID_PARAM;
    }

    // Check if controllers are busy
    if (sdio_is_cmd_busy() || sdio_is_data_busy()) {
        return SDIO_ERROR_BUSY;
    }

    // Write data to buffer
    uint16_t word_count = (data_len + 3) / 4;  // Round up to word count
    for (uint16_t i = 0; i < word_count; i++) {
        sdio_write_reg(SDIO_DATA_BUFFER_OFFSET + (i * 4), data_buf[i]);
    }

    // Set data length
    sdio_write_reg(SDIO_DATA_LENGTH_OFFSET, data_len);

    // Set command index and argument
    sdio_write_reg(SDIO_CMD_INDEX_OFFSET, cmd_index);
    sdio_write_reg(SDIO_CMD_ARGUMENT_OFFSET, arg);

    // Trigger combined command + data write operation
    sdio_read_reg(SDIO_SEND_CMD_SEND_DATA_OP_OFFSET);

    // Wait for both command and data completion
    sdio_wait_cmd_ready();
    sdio_wait_data_ready();

    // Check command status
    status = sdio_get_cmd_status(resp);
    if (status != SDIO_OK) {
        return status;
    }

    // Check data status
    return sdio_get_data_status();
}

sdio_status_t sdio_read_data(uint32_t *data_buf, uint16_t data_len) {
    // Validate parameters
    if (!data_buf || data_len == 0 || data_len > SDIO_DATA_BUFFER_SIZE_BYTES) {
        return SDIO_ERROR_INVALID_PARAM;
    }

    // Check if data controller is busy
    if (sdio_is_data_busy()) {
        return SDIO_ERROR_BUSY;
    }

    // Set data length
    sdio_write_reg(SDIO_DATA_LENGTH_OFFSET, data_len);

    // Trigger data read operation
    sdio_read_reg(SDIO_READ_DATA_OP_OFFSET);

    // Wait for data completion
    sdio_wait_data_ready();

    // Check data status
    sdio_status_t status = sdio_get_data_status();
    if (status != SDIO_OK) {
        return status;
    }

    // Read data from buffer
    uint16_t word_count = (data_len + 3) / 4;
    for (uint16_t i = 0; i < word_count; i++) {
        data_buf[i] = sdio_read_reg(SDIO_DATA_BUFFER_OFFSET + (i * 4));
    }

    return SDIO_OK;
}

sdio_status_t sdio_write_data(const uint32_t *data_buf, uint16_t data_len) {
    // Validate parameters
    if (!data_buf || data_len == 0 || data_len > SDIO_DATA_BUFFER_SIZE_BYTES) {
        return SDIO_ERROR_INVALID_PARAM;
    }

    // Check if data controller is busy
    if (sdio_is_data_busy()) {
        return SDIO_ERROR_BUSY;
    }

    // Write data to buffer
    uint16_t word_count = (data_len + 3) / 4;
    for (uint16_t i = 0; i < word_count; i++) {
        sdio_write_reg(SDIO_DATA_BUFFER_OFFSET + (i * 4), data_buf[i]);
    }

    // Set data length
    sdio_write_reg(SDIO_DATA_LENGTH_OFFSET, data_len);

    // Trigger data write operation
    sdio_read_reg(SDIO_SEND_DATA_OP_OFFSET);

    // Wait for data completion
    sdio_wait_data_ready();

    // Check data status
    return sdio_get_data_status();
}
