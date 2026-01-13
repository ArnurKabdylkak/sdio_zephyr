#include "sdio_hal.h"
#include <stdio.h>

// Example: Basic SDIO initialization sequence
// This demonstrates typical SDIO WiFi module initialization

void print_response(const char *name, sdio_response_t *resp) {
    if (resp->timeout) {
        printf("%s: TIMEOUT\n", name);
    } else {
        printf("%s: idx=0x%02x arg=0x%08x\n", name, resp->index, resp->arg[0]);
    }
}

int main(void) {
    sdio_response_t resp;
    sdio_status_t status;

    printf("SDIO HAL Example\n");

    // Initialize SDIO with 48MHz main clock, 100kHz SD clock (for initialization)
    sdio_init(48000000, 100000);
    printf("SDIO initialized: main_clk=48MHz, sd_clk=100kHz\n");

    // CMD0: GO_IDLE_STATE - Reset card
    status = sdio_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, &resp);
    if (status == SDIO_OK) {
        print_response("CMD0", &resp);
    } else {
        printf("CMD0 failed: %d\n", status);
    }

    // Small delay for card to reset (implement delay function as needed)
    for (volatile int i = 0; i < 10000; i++);

    // CMD5: IO_SEND_OP_COND - Get SDIO card operating voltage
    status = sdio_send_cmd(SD_CMD5_IO_SEND_OP_COND, 0, &resp);
    if (status == SDIO_OK) {
        print_response("CMD5", &resp);
        uint32_t ocr = resp.arg[0];
        printf("  OCR: 0x%08x\n", ocr);

        // Check if card is ready (bit 31)
        if (ocr & 0x80000000) {
            printf("  Card is ready\n");
        }

        // Number of I/O functions (bits 30-28)
        uint8_t num_funcs = (ocr >> 28) & 0x7;
        printf("  I/O Functions: %d\n", num_funcs);
    } else {
        printf("CMD5 failed: %d\n", status);
    }

    // CMD5 with operating voltage (typical: 0x00300000 for 3.2-3.4V)
    status = sdio_send_cmd(SD_CMD5_IO_SEND_OP_COND, 0x00300000, &resp);
    if (status == SDIO_OK) {
        print_response("CMD5 (with voltage)", &resp);
    }

    // CMD3: SEND_RELATIVE_ADDR - Get card address
    status = sdio_send_cmd(SD_CMD3_SEND_RELATIVE_ADDR, 0, &resp);
    if (status == SDIO_OK) {
        print_response("CMD3", &resp);
        uint16_t rca = (resp.arg[0] >> 16) & 0xFFFF;
        printf("  RCA: 0x%04x\n", rca);

        // CMD7: SELECT_CARD - Select card with RCA
        status = sdio_send_cmd(SD_CMD7_SELECT_CARD, rca << 16, &resp);
        if (status == SDIO_OK) {
            print_response("CMD7", &resp);
        }
    }

    // Increase clock speed for data transfer (e.g., 25MHz)
    sdio_set_clock_freq(25000000);
    printf("Clock speed increased to 25MHz\n");

    // Example: CMD52 - Direct I/O read/write
    // Read CCCR (Card Common Control Registers) at address 0x00
    uint32_t cmd52_arg = (0 << 31) |    // R/W flag: 0=read
                         (0 << 28) |    // Function: 0=CIA
                         (0 << 27) |    // RAW flag
                         (0x00 << 9) |  // Register address
                         0;             // Write data
    status = sdio_send_cmd(SD_CMD52_IO_RW_DIRECT, cmd52_arg, &resp);
    if (status == SDIO_OK) {
        print_response("CMD52 (read CCCR)", &resp);
        uint8_t cccr_data = resp.arg[0] & 0xFF;
        printf("  CCCR data: 0x%02x\n", cccr_data);
    }

    // Example: CMD53 with data transfer
    // Read 64 bytes from function 1, address 0x1000
    uint32_t data_buf[16];  // 64 bytes = 16 words
    uint32_t cmd53_arg = (0 << 31) |     // R/W flag: 0=read
                         (1 << 28) |     // Function: 1
                         (0 << 27) |     // Block mode: 0=byte mode
                         (0 << 26) |     // OP Code: 0=fixed addr, 1=incrementing
                         (0x1000 << 9) | // Register address
                         64;             // Byte count

    status = sdio_send_cmd_with_data_read(SD_CMD53_IO_RW_EXTENDED, cmd53_arg,
                                          data_buf, 64, &resp);
    if (status == SDIO_OK) {
        print_response("CMD53 (read 64 bytes)", &resp);
        printf("  Data: ");
        for (int i = 0; i < 4; i++) {
            printf("0x%08x ", data_buf[i]);
        }
        printf("...\n");
    } else {
        printf("CMD53 failed: %d\n", status);
    }

    printf("SDIO example completed\n");
    return 0;
}
