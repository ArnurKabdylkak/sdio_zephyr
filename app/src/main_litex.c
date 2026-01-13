/**
 * LiteX SDIO HAL Integration Example for Zephyr
 *
 * Этот файл показывает, как использовать HAL из litex/ в Zephyr приложении
 * для работы с кастомным SDIO контроллером на LiteX SoC.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(litex_sdio, CONFIG_LOG_DEFAULT_LEVEL);

/* Подключаем LiteX SDIO HAL */
#define SDIO_BASE 0x80000000  /* Из sipeed_tang_primer_20k.py, строка 247 */
#include "sdio_hal.h"

/*============================================================================
 * SDIO Initialization Sequence
 *============================================================================*/

static int sdio_init_card(void)
{
    sdio_response_t resp;
    sdio_status_t status;
    uint32_t ocr;
    uint16_t rca;

    LOG_INF("=== SDIO Card Initialization ===");

    /* CMD0 - GO_IDLE_STATE */
    LOG_INF("Sending CMD0 (GO_IDLE_STATE)...");
    status = sdio_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, &resp);
    if (status != SDIO_OK) {
        LOG_ERR("CMD0 failed: %d", status);
        return -1;
    }
    k_msleep(10);

    /* CMD5 - IO_SEND_OP_COND (query voltage) */
    LOG_INF("Sending CMD5 (IO_SEND_OP_COND)...");
    for (int attempt = 0; attempt < 5; attempt++) {
        status = sdio_send_cmd(SD_CMD5_IO_SEND_OP_COND, 0, &resp);
        if (status == SDIO_OK && !resp.timeout) {
            LOG_INF("CMD5 response received!");
            break;
        }
        LOG_WRN("CMD5 attempt %d failed", attempt + 1);
        k_msleep(50);
    }

    if (status != SDIO_OK || resp.timeout) {
        LOG_ERR("CMD5 failed after retries");
        return -1;
    }

    /* Parse OCR from R4 response */
    ocr = resp.arg[0] & 0x00FFFFFF;
    uint8_t num_io = (resp.arg[0] >> 28) & 0x7;
    LOG_INF("OCR=0x%06x, IO functions=%d", ocr, num_io);

    /* CMD5 - Set voltage window and wait for ready */
    LOG_INF("Sending CMD5 with voltage...");
    for (int i = 0; i < 100; i++) {
        status = sdio_send_cmd(SD_CMD5_IO_SEND_OP_COND, ocr, &resp);
        if (status == SDIO_OK && !resp.timeout) {
            /* Check C (Card Ready) bit */
            if (resp.arg[0] & (1 << 31)) {
                LOG_INF("Card ready! OCR=0x%08x", resp.arg[0]);
                break;
            }
        }
        k_msleep(10);
    }

    if (!(resp.arg[0] & (1 << 31))) {
        LOG_ERR("Card not ready");
        return -1;
    }

    /* CMD3 - SEND_RELATIVE_ADDR */
    LOG_INF("Sending CMD3 (SEND_RELATIVE_ADDR)...");
    status = sdio_send_cmd(SD_CMD3_SEND_RELATIVE_ADDR, 0, &resp);
    if (status != SDIO_OK || resp.timeout) {
        LOG_ERR("CMD3 failed");
        return -1;
    }

    /* Parse RCA from R6 response */
    rca = (resp.arg[0] >> 16) & 0xFFFF;
    LOG_INF("RCA = 0x%04x", rca);

    k_msleep(10);

    /* CMD7 - SELECT_CARD */
    LOG_INF("Sending CMD7 (SELECT_CARD) with RCA=0x%04x...", rca);
    status = sdio_send_cmd(SD_CMD7_SELECT_CARD, (uint32_t)rca << 16, &resp);
    if (status != SDIO_OK || resp.timeout) {
        LOG_ERR("CMD7 failed");
        return -1;
    }

    LOG_INF("Card selected!");

    /* Increase clock frequency for data transfer */
    LOG_INF("Increasing SDIO clock to 25MHz...");
    sdio_set_clock_freq(25000000);

    return 0;
}

/*============================================================================
 * CMD52 - Single byte I/O operations
 *============================================================================*/

static int cmd52_read(uint8_t func, uint32_t addr, uint8_t *val)
{
    /* Build CMD52 argument for read */
    uint32_t arg = 0;
    arg |= ((func & 0x7) << 28);           /* Function number */
    arg |= ((addr & 0x1FFFF) << 9);        /* Register address */
    /* R/W bit = 0 for read */

    sdio_response_t resp;
    sdio_status_t status = sdio_send_cmd(SD_CMD52_IO_RW_DIRECT, arg, &resp);

    if (status != SDIO_OK || resp.timeout) {
        LOG_ERR("CMD52 read failed: func=%d addr=0x%05x", func, addr);
        return -1;
    }

    /* Parse R5 response */
    uint8_t flags = (resp.arg[0] >> 8) & 0xFF;
    *val = resp.arg[0] & 0xFF;

    LOG_DBG("CMD52 READ: func=%d addr=0x%05x -> val=0x%02x flags=0x%02x",
            func, addr, *val, flags);

    /* Check error flags (bits 7:6 = error, bit 3 = illegal cmd, etc) */
    if (flags & 0xCB) {
        LOG_WRN("CMD52 flags indicate error: 0x%02x", flags);
        return -1;
    }

    return 0;
}

static int cmd52_write(uint8_t func, uint32_t addr, uint8_t val)
{
    /* Build CMD52 argument for write */
    uint32_t arg = 0;
    arg |= (1U << 31);                     /* R/W = 1 for write */
    arg |= ((func & 0x7) << 28);           /* Function number */
    arg |= ((addr & 0x1FFFF) << 9);        /* Register address */
    arg |= val;                            /* Data to write */

    sdio_response_t resp;
    sdio_status_t status = sdio_send_cmd(SD_CMD52_IO_RW_DIRECT, arg, &resp);

    if (status != SDIO_OK || resp.timeout) {
        LOG_ERR("CMD52 write failed: func=%d addr=0x%05x val=0x%02x",
                func, addr, val);
        return -1;
    }

    uint8_t flags = (resp.arg[0] >> 8) & 0xFF;

    LOG_DBG("CMD52 WRITE: func=%d addr=0x%05x val=0x%02x -> flags=0x%02x",
            func, addr, val, flags);

    if (flags & 0xCB) {
        LOG_WRN("CMD52 flags indicate error: 0x%02x", flags);
        return -1;
    }

    return 0;
}

/*============================================================================
 * Read CCCR (Card Common Control Registers)
 *============================================================================*/

static void read_cccr(void)
{
    uint8_t val;

    LOG_INF("=== Reading CCCR ===");

    /* CCCR/SDIO Revision (0x00) */
    if (cmd52_read(0, 0x00, &val) == 0) {
        LOG_INF("CCCR/SDIO Rev: 0x%02x (CCCR=%d.%d, SDIO=%d.%d)",
                val, (val >> 4) & 0xF, val & 0xF,
                (val >> 4) & 0xF, val & 0xF);
    }

    /* SD Spec Revision (0x01) */
    if (cmd52_read(0, 0x01, &val) == 0) {
        LOG_INF("SD Spec Rev: 0x%02x", val);
    }

    /* I/O Enable (0x02) */
    if (cmd52_read(0, 0x02, &val) == 0) {
        LOG_INF("I/O Enable: 0x%02x", val);
    }

    /* I/O Ready (0x03) */
    if (cmd52_read(0, 0x03, &val) == 0) {
        LOG_INF("I/O Ready: 0x%02x", val);
    }

    /* Interrupt Enable (0x04) */
    if (cmd52_read(0, 0x04, &val) == 0) {
        LOG_INF("Int Enable: 0x%02x", val);
    }

    /* Bus Interface Control (0x07) */
    if (cmd52_read(0, 0x07, &val) == 0) {
        LOG_INF("Bus Interface: 0x%02x", val);
    }

    /* Card Capability (0x08) */
    if (cmd52_read(0, 0x08, &val) == 0) {
        LOG_INF("Card Capability: 0x%02x", val);
    }

    /* High Speed (0x13) */
    if (cmd52_read(0, 0x13, &val) == 0) {
        LOG_INF("High Speed: 0x%02x", val);
    }
}

/*============================================================================
 * Main Application
 *============================================================================*/

int main(void)
{
    int ret;

    LOG_INF("========================================");
    LOG_INF("LiteX SDIO Controller Test");
    LOG_INF("Platform: LiteX VexRiscv SoC");
    LOG_INF("SDIO Base: 0x%08x", SDIO_BASE);
    LOG_INF("========================================");

    /* Initialize SDIO HAL */
    LOG_INF("Initializing SDIO HAL...");
    sdio_init(48000000, 100000);  /* 48MHz system clock, 100kHz SDIO init */

    /* Wait for hardware to stabilize */
    k_msleep(100);

    /* Initialize SDIO card/module */
    ret = sdio_init_card();
    if (ret < 0) {
        LOG_ERR("SDIO card initialization failed!");
        LOG_INF("Troubleshooting:");
        LOG_INF("  1. Check SDIO module is powered");
        LOG_INF("  2. Verify HDL is loaded on FPGA");
        LOG_INF("  3. Check wishbone connections in LiteX");
        goto done;
    }

    /* Read CCCR registers */
    read_cccr();

    /* Enable Function 1 (backplane) */
    LOG_INF("=== Enabling Function 1 ===");
    if (cmd52_write(0, 0x02, 0x02) == 0) {
        LOG_INF("Wrote IOE=0x02 (Enable Function 1)");

        /* Wait for Function 1 ready */
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

    LOG_INF("========================================");
    LOG_INF("SDIO Initialization Complete!");
    LOG_INF("========================================");

    LOG_INF("Next steps:");
    LOG_INF("  - Implement CMD53 for multi-byte transfer");
    LOG_INF("  - Read backplane to get chip ID");
    LOG_INF("  - Download firmware to CYW55500");

done:
    /* Main loop */
    while (1) {
        k_msleep(1000);
    }

    return 0;
}
