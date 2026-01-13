#include "sdio_hal.h"
#include "sdio_utils.h"
#include <stdio.h>
#include <string.h>

// Simple delay function (adjust for your clock speed)
static void delay_ms(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 1000; i++);
}

// Print binary data in hex dump format
static void print_hex_dump(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        if (i % 16 == 0) {
            printf("\n%04x: ", i);
        }
        printf("%02x ", data[i]);
    }
    printf("\n");
}

// Complete SDIO WiFi module initialization
int sdio_wifi_init(void) {
    sdio_response_t resp;
    sdio_status_t status;
    uint16_t rca = 0;

    printf("=== SDIO WiFi Module Initialization ===\n\n");

    // Step 1: Initialize SDIO with low clock (100-400 kHz for identification)
    printf("1. Initializing SDIO controller...\n");
    sdio_init(48000000, 100000);
    printf("   Main clock: 48 MHz, SDIO clock: 100 kHz\n");
    delay_ms(10);

    // Step 2: CMD0 - Reset to idle state
    printf("\n2. Sending CMD0 (GO_IDLE_STATE)...\n");
    status = sdio_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, &resp);
    if (status != SDIO_OK) {
        printf("   ERROR: CMD0 failed (status=%d)\n", status);
        return -1;
    }
    printf("   OK\n");
    delay_ms(10);

    // Step 3: CMD5 - Check if SDIO card and get OCR
    printf("\n3. Sending CMD5 (IO_SEND_OP_COND) - inquiry...\n");
    status = sdio_send_cmd(SD_CMD5_IO_SEND_OP_COND, 0, &resp);
    if (status != SDIO_OK || resp.timeout) {
        printf("   ERROR: CMD5 failed - not an SDIO card?\n");
        return -1;
    }

    sdio_cmd5_response_t cmd5_resp;
    sdio_parse_cmd5_response(resp.arg[0], &cmd5_resp);
    printf("   OCR: 0x%08x\n", resp.arg[0]);
    printf("   Card ready: %s\n", cmd5_resp.card_ready ? "yes" : "no");
    printf("   Number of I/O functions: %d\n", cmd5_resp.num_functions);
    printf("   Memory present: %s\n", cmd5_resp.memory_present ? "yes" : "no");

    // Step 4: CMD5 - Set operating voltage (3.2-3.4V)
    printf("\n4. Sending CMD5 with voltage range...\n");
    uint32_t voltage = 0x00300000;  // 3.2-3.4V
    status = sdio_send_cmd(SD_CMD5_IO_SEND_OP_COND, voltage, &resp);
    if (status != SDIO_OK) {
        printf("   ERROR: CMD5 with voltage failed\n");
        return -1;
    }
    sdio_parse_cmd5_response(resp.arg[0], &cmd5_resp);
    if (!cmd5_resp.card_ready) {
        printf("   ERROR: Card not ready after voltage selection\n");
        return -1;
    }
    printf("   Card ready for operation\n");

    // Step 5: CMD3 - Get Relative Card Address (RCA)
    printf("\n5. Sending CMD3 (SEND_RELATIVE_ADDR)...\n");
    status = sdio_send_cmd(SD_CMD3_SEND_RELATIVE_ADDR, 0, &resp);
    if (status != SDIO_OK) {
        printf("   ERROR: CMD3 failed\n");
        return -1;
    }
    rca = (resp.arg[0] >> 16) & 0xFFFF;
    printf("   RCA: 0x%04x\n", rca);

    // Step 6: CMD7 - Select card
    printf("\n6. Sending CMD7 (SELECT_CARD)...\n");
    status = sdio_send_cmd(SD_CMD7_SELECT_CARD, rca << 16, &resp);
    if (status != SDIO_OK) {
        printf("   ERROR: CMD7 failed\n");
        return -1;
    }
    printf("   Card selected\n");

    // Step 7: Read CCCR registers
    printf("\n7. Reading CCCR registers...\n");
    uint8_t cccr_rev, sd_spec, card_cap;

    if (sdio_read_cccr(CCCR_SDIO_REVISION, &cccr_rev) != SDIO_OK) {
        printf("   ERROR: Failed to read CCCR revision\n");
        return -1;
    }
    printf("   CCCR/SDIO revision: 0x%02x\n", cccr_rev);

    if (sdio_read_cccr(CCCR_SD_SPEC_REVISION, &sd_spec) != SDIO_OK) {
        printf("   ERROR: Failed to read SD spec\n");
        return -1;
    }
    printf("   SD spec revision: 0x%02x\n", sd_spec);

    if (sdio_read_cccr(CCCR_CARD_CAPABILITY, &card_cap) != SDIO_OK) {
        printf("   ERROR: Failed to read card capability\n");
        return -1;
    }
    printf("   Card capability: 0x%02x\n", card_cap);
    printf("     - Direct commands (CMD52): %s\n", (card_cap & 0x01) ? "yes" : "no");
    printf("     - Multi-block (CMD53): %s\n", (card_cap & 0x02) ? "yes" : "no");
    printf("     - Low-speed card: %s\n", (card_cap & 0x40) ? "yes" : "no");
    printf("     - 4-bit mode: %s\n", (card_cap & 0x80) ? "yes" : "no");

    // Step 8: Enable 4-bit bus width
    if (card_cap & 0x80) {
        printf("\n8. Enabling 4-bit bus mode...\n");
        status = sdio_set_bus_width(BUS_WIDTH_4BIT);
        if (status != SDIO_OK) {
            printf("   WARNING: Failed to set 4-bit mode\n");
        } else {
            printf("   4-bit mode enabled\n");
        }
    }

    // Step 9: Increase clock speed
    printf("\n9. Increasing clock speed to 25 MHz...\n");
    sdio_set_clock_freq(25000000);
    delay_ms(1);
    printf("   Clock speed: %d Hz\n", sdio_get_clock_freq());

    // Step 10: Enable Function 1 (WiFi function)
    printf("\n10. Enabling Function 1...\n");
    status = sdio_enable_function(1, true);
    if (status != SDIO_OK) {
        printf("   ERROR: Failed to enable function 1\n");
        return -1;
    }

    // Wait for function to be ready
    bool ready = false;
    for (int i = 0; i < 10; i++) {
        delay_ms(10);
        if (sdio_is_function_ready(1, &ready) == SDIO_OK && ready) {
            break;
        }
    }

    if (!ready) {
        printf("   ERROR: Function 1 not ready\n");
        return -1;
    }
    printf("   Function 1 is ready\n");

    // Step 11: Set block size for function 1 (typically 512 bytes)
    printf("\n11. Setting block size to 512 bytes...\n");
    status = sdio_set_block_size(1, 512);
    if (status != SDIO_OK) {
        printf("   WARNING: Failed to set block size\n");
    } else {
        printf("   Block size set to 512 bytes\n");
    }

    // Step 12: Enable interrupts for function 1
    printf("\n12. Enabling interrupts for Function 1...\n");
    status = sdio_enable_interrupt(1, true);
    if (status != SDIO_OK) {
        printf("   WARNING: Failed to enable interrupts\n");
    } else {
        printf("   Interrupts enabled\n");
    }

    printf("\n=== SDIO Initialization Complete ===\n\n");
    return 0;
}

// Example: Read data from WiFi module using CMD53
int sdio_read_wifi_data_example(void) {
    uint32_t data_buf[128];  // 512 bytes
    sdio_response_t resp;

    printf("=== Reading data from WiFi module ===\n");

    // Read 512 bytes from Function 1, address 0x0000 (example)
    uint32_t cmd53_arg = sdio_cmd53_arg(
        false,      // read
        1,          // function 1
        true,       // block mode
        true,       // incrementing address
        0x0000,     // start address
        1           // 1 block (512 bytes)
    );

    sdio_status_t status = sdio_send_cmd_with_data_read(
        SD_CMD53_IO_RW_EXTENDED,
        cmd53_arg,
        data_buf,
        512,
        &resp
    );

    if (status != SDIO_OK) {
        printf("ERROR: CMD53 read failed (status=%d)\n", status);
        return -1;
    }

    printf("Data read successfully:\n");
    print_hex_dump((uint8_t*)data_buf, 64);  // Print first 64 bytes

    return 0;
}

// Example: Write data to WiFi module using CMD53
int sdio_write_wifi_data_example(void) {
    uint32_t data_buf[128];  // 512 bytes
    sdio_response_t resp;

    printf("=== Writing data to WiFi module ===\n");

    // Fill buffer with example data
    for (int i = 0; i < 128; i++) {
        data_buf[i] = 0x11223344 + i;
    }

    // Write 512 bytes to Function 1, address 0x0000 (example)
    uint32_t cmd53_arg = sdio_cmd53_arg(
        true,       // write
        1,          // function 1
        true,       // block mode
        true,       // incrementing address
        0x0000,     // start address
        1           // 1 block (512 bytes)
    );

    sdio_status_t status = sdio_send_cmd_with_data_write(
        SD_CMD53_IO_RW_EXTENDED,
        cmd53_arg,
        data_buf,
        512,
        &resp
    );

    if (status != SDIO_OK) {
        printf("ERROR: CMD53 write failed (status=%d)\n", status);
        return -1;
    }

    printf("Data written successfully\n");
    return 0;
}

int main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║  SDIO HAL Advanced Example             ║\n");
    printf("║  LiteX SDIO WiFi Controller            ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");

    // Initialize WiFi module
    if (sdio_wifi_init() != 0) {
        printf("\nFailed to initialize WiFi module\n");
        return -1;
    }

    // Example data operations
    printf("\n--- Example Data Operations ---\n\n");

    // Read example
    if (sdio_read_wifi_data_example() != 0) {
        printf("Read example failed\n");
    }

    // Write example
    if (sdio_write_wifi_data_example() != 0) {
        printf("Write example failed\n");
    }

    printf("\n=== All operations completed ===\n");
    return 0;
}
