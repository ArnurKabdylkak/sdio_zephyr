/**
 * SDIO Host Controller - LiteX/RISC-V Platform Layer
 *
 * This file provides the platform-specific SDIO implementation
 * for LiteX-based RISC-V SoCs.
 *
 * TODO: Implement based on your specific SDIO controller
 */

#ifndef SDIO_LITEX_H
#define SDIO_LITEX_H

#include <stdint.h>
#include <stdbool.h>
#include "cyw55500_sdio.h"

/*============================================================================
 * LiteX SDIO Controller Base Address
 *============================================================================*/

#ifndef SDIO_BASE
#define SDIO_BASE               0x80000000
#endif

/*============================================================================
 * LiteX SDIO Controller Registers
 *============================================================================*/

/* Clock registers */
#define SDIO_REG_MAIN_CLK_FREQ      (SDIO_BASE + 0x0000)  /* Main clock frequency (read-only) */
#define SDIO_REG_SDIO_CLK_FREQ      (SDIO_BASE + 0x1000)  /* SDIO clock frequency (read-only) */

/* Command registers */
#define SDIO_REG_CMD_INDEX          (SDIO_BASE + 0x2000)  /* Command index (write) / Response index (read) */
#define SDIO_REG_CMD_ARGUMENT       (SDIO_BASE + 0x3000)  /* Command argument (write) / Response argument (read) */

/* Data buffer */
#define SDIO_REG_DATA_BUFFER        (SDIO_BASE + 0x4000)  /* Data buffer (array of 32-bit words) */

/* Operation triggers (write to start, read returns 0 when ready) */
#define SDIO_REG_SEND_CMD           (SDIO_BASE + 0x5000)  /* Send command only */
#define SDIO_REG_SEND_CMD_READ_DATA (SDIO_BASE + 0x6000)  /* Send command and read data */
#define SDIO_REG_SEND_CMD_SEND_DATA (SDIO_BASE + 0x7000)  /* Send command and send data */
#define SDIO_REG_READ_DATA          (SDIO_BASE + 0x8000)  /* Read data only */
#define SDIO_REG_SEND_DATA          (SDIO_BASE + 0x9000)  /* Send data only */

/* Status registers */
#define SDIO_REG_CMD_BUSY           (SDIO_BASE + 0xa000)  /* Command busy (1 = busy) */
#define SDIO_REG_DATA_BUSY          (SDIO_BASE + 0xb000)  /* Data busy (1 = busy) */
#define SDIO_REG_CMD_STATUS         (SDIO_BASE + 0xc000)  /* Command status */
#define SDIO_REG_DATA_STATUS        (SDIO_BASE + 0xd000)  /* Data status */

/* Data length register */
#define SDIO_REG_DATA_LENGTH        (SDIO_BASE + 0xe000)  /* Data length in bytes */

/* Command status bits */
#define SDIO_CMD_STATUS_TIMEOUT     (1 << 0)  /* Command timeout */
#define SDIO_CMD_STATUS_INDEX_MASK  0xFE      /* Response index (bits 7:1) */
#define SDIO_CMD_STATUS_INDEX_SHIFT 1

/* Data status bits */
#define SDIO_DATA_STATUS_CRC_ERROR  (1 << 0)  /* CRC error */
#define SDIO_DATA_STATUS_TIMEOUT    (1 << 1)  /* Data timeout */

/*============================================================================
 * Register Access Macros
 *============================================================================*/

#define REG32(addr)             (*(volatile uint32_t *)(addr))
#define REG16(addr)             (*(volatile uint16_t *)(addr))
#define REG8(addr)              (*(volatile uint8_t *)(addr))

/*============================================================================
 * Timer Access (LiteX specific)
 * TODO: Update based on your timer configuration
 *============================================================================*/

#ifndef TIMER_BASE
#define TIMER_BASE              0x82001000  /* Example: Update from csr.h */
#endif

/* Simple busy-wait delay using CPU cycles */
static inline void litex_delay_us(uint32_t us)
{
    /* Assuming ~100MHz CPU, adjust for your clock */
    volatile uint32_t count = us * 100;
    while (count--) {
        __asm__ volatile ("nop");
    }
}

static inline void litex_delay_ms(uint32_t ms)
{
    while (ms--) {
        litex_delay_us(1000);
    }
}

/*============================================================================
 * SDIO Commands
 *============================================================================*/

/* SD/SDIO commands */
#define CMD0_GO_IDLE            0
#define CMD3_SEND_RCA           3
#define CMD5_IO_SEND_OP_COND    5
#define CMD7_SELECT_CARD        7
#define CMD52_IO_RW_DIRECT      52
#define CMD53_IO_RW_EXTENDED    53

/* Response types */
#define RSP_NONE                0
#define RSP_R1                  1
#define RSP_R4                  4
#define RSP_R5                  5
#define RSP_R6                  6

/*============================================================================
 * Platform Implementation Prototypes
 *============================================================================*/

/**
 * Initialize LiteX SDIO host controller
 */
int litex_sdio_init(void);

/**
 * Deinitialize SDIO host
 */
void litex_sdio_deinit(void);

/**
 * Send CMD52 (IO_RW_DIRECT) - single byte read/write
 */
int litex_sdio_cmd52_read(uint8_t func, uint32_t addr, uint8_t *val);
int litex_sdio_cmd52_write(uint8_t func, uint32_t addr, uint8_t val);

/**
 * Send CMD53 (IO_RW_EXTENDED) - multi-byte read/write
 */
int litex_sdio_cmd53_read(uint8_t func, uint32_t addr, uint8_t *data,
                          uint32_t len, bool incr_addr);
int litex_sdio_cmd53_write(uint8_t func, uint32_t addr, const uint8_t *data,
                           uint32_t len, bool incr_addr);

/**
 * Set block size for function
 */
int litex_sdio_set_block_size(uint8_t func, uint16_t block_size);

/**
 * Enable/disable function
 */
int litex_sdio_enable_func(uint8_t func, bool enable);

/**
 * Enable/disable interrupts
 */
int litex_sdio_enable_irq(bool enable);

/**
 * Check if interrupt pending
 */
bool litex_sdio_irq_pending(void);

/*============================================================================
 * Platform Operations Structure
 *============================================================================*/

/**
 * Get the LiteX SDIO operations structure
 * Use this to initialize the CYW55500 driver
 */
const sdio_host_ops_t *litex_get_sdio_ops(void);

#endif /* SDIO_LITEX_H */
