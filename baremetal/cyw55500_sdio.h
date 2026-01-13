/**
 * CYW55500 WiFi - SDIO Driver Interface
 * Bare-metal driver for RISC-V SoC
 */

#ifndef CYW55500_SDIO_H
#define CYW55500_SDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "cyw55500_regs.h"

/*============================================================================
 * Configuration
 *============================================================================*/

#define CYW55500_FW_PATH            "cyfmac55500-sdio.bin"
#define CYW55500_NVRAM_PATH         "cyfmac55500-sdio.txt"

#define SDIO_MAX_BLOCK_SIZE         512
#define SDIO_F1_BLOCK_SIZE          64
#define SDIO_F2_BLOCK_SIZE          512

#define TX_BUF_SIZE                 2048
#define RX_BUF_SIZE                 2048

/*============================================================================
 * Error Codes
 *============================================================================*/

typedef enum {
    CYW_OK = 0,
    CYW_ERROR = -1,
    CYW_ERR_TIMEOUT = -2,
    CYW_ERR_INVALID = -3,
    CYW_ERR_NOMEM = -4,
    CYW_ERR_BUSY = -5,
    CYW_ERR_IO = -6,
    CYW_ERR_FW = -7,
    CYW_ERR_NOT_READY = -8,
} cyw_err_t;

/*============================================================================
 * Driver State
 *============================================================================*/

typedef enum {
    CYW_STATE_OFF = 0,
    CYW_STATE_INIT,
    CYW_STATE_FW_LOADING,
    CYW_STATE_FW_READY,
    CYW_STATE_UP,
    CYW_STATE_ERROR,
} cyw_state_t;

/*============================================================================
 * Chip Information
 *============================================================================*/

typedef struct {
    uint32_t chip_id;
    uint32_t chip_rev;
    uint32_t enum_base;
    uint32_t ram_base;
    uint32_t ram_size;
    uint32_t cc_caps;
    uint32_t pmu_caps;
    uint32_t pmu_rev;
} cyw_chip_info_t;

/*============================================================================
 * Core Information
 *============================================================================*/

typedef struct {
    uint16_t id;
    uint16_t rev;
    uint32_t base;
    uint32_t wrap;
} cyw_core_t;

/*============================================================================
 * SDPCM Header
 *============================================================================*/

typedef struct __attribute__((packed)) {
    uint16_t len;
    uint16_t len_check;     /* ~len */
    uint8_t  seq;
    uint8_t  channel;
    uint8_t  next_len;
    uint8_t  data_offset;
    uint8_t  flow_control;
    uint8_t  max_seq;
    uint8_t  reserved[2];
} sdpcm_header_t;

#define SDPCM_HEADER_SIZE   sizeof(sdpcm_header_t)

/*============================================================================
 * BCDC Header
 *============================================================================*/

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t len;
    uint32_t flags;
    uint32_t status;
} bcdc_header_t;

#define BCDC_HEADER_SIZE    sizeof(bcdc_header_t)

/*============================================================================
 * SDIO Host Operations (Platform Specific)
 *
 * These functions must be implemented by the platform layer
 *============================================================================*/

typedef struct {
    /* Initialize SDIO host controller */
    int (*init)(void);

    /* Deinitialize SDIO host controller */
    void (*deinit)(void);

    /* CMD52: Read single byte (func, addr) -> value */
    int (*cmd52_read)(uint8_t func, uint32_t addr, uint8_t *val);

    /* CMD52: Write single byte (func, addr, value) */
    int (*cmd52_write)(uint8_t func, uint32_t addr, uint8_t val);

    /* CMD53: Read multiple bytes (func, addr, data, len, incr_addr) */
    int (*cmd53_read)(uint8_t func, uint32_t addr, uint8_t *data,
                      uint32_t len, bool incr_addr);

    /* CMD53: Write multiple bytes (func, addr, data, len, incr_addr) */
    int (*cmd53_write)(uint8_t func, uint32_t addr, const uint8_t *data,
                       uint32_t len, bool incr_addr);

    /* Set block size for function */
    int (*set_block_size)(uint8_t func, uint16_t block_size);

    /* Enable/disable function */
    int (*enable_func)(uint8_t func, bool enable);

    /* Enable/disable interrupts */
    int (*enable_irq)(bool enable);

    /* Check interrupt pending */
    bool (*irq_pending)(void);

    /* Delay microseconds */
    void (*delay_us)(uint32_t us);

    /* Delay milliseconds */
    void (*delay_ms)(uint32_t ms);
} sdio_host_ops_t;

/*============================================================================
 * Driver Context
 *============================================================================*/

typedef struct {
    /* State */
    cyw_state_t state;

    /* Chip info */
    cyw_chip_info_t chip;

    /* Core addresses */
    cyw_core_t core_cc;         /* ChipCommon */
    cyw_core_t core_sdio;       /* SDIO Device */
    cyw_core_t core_arm;        /* ARM */
    cyw_core_t core_ram;        /* RAM */

    /* Current backplane window */
    uint32_t sbwad;
    bool sbwad_valid;

    /* SDPCM state */
    uint8_t tx_seq;
    uint8_t rx_seq;
    uint8_t tx_max;
    uint8_t flow_ctrl;

    /* BCDC state */
    uint16_t reqid;

    /* Buffers */
    uint8_t tx_buf[TX_BUF_SIZE] __attribute__((aligned(4)));
    uint8_t rx_buf[RX_BUF_SIZE] __attribute__((aligned(4)));

    /* Platform operations */
    const sdio_host_ops_t *ops;

} cyw_dev_t;

/*============================================================================
 * Public API
 *============================================================================*/

/**
 * Initialize driver with platform operations
 * @param ops Platform SDIO operations
 * @return CYW_OK on success
 */
cyw_err_t cyw_init(const sdio_host_ops_t *ops);

/**
 * Deinitialize driver
 */
void cyw_deinit(void);

/**
 * Get chip information
 * @param info Pointer to store chip info
 * @return CYW_OK on success
 */
cyw_err_t cyw_get_chip_info(cyw_chip_info_t *info);

/**
 * Load firmware
 * @param fw_data Firmware binary data
 * @param fw_size Firmware size
 * @param nvram_data NVRAM data (text)
 * @param nvram_size NVRAM size
 * @return CYW_OK on success
 */
cyw_err_t cyw_load_firmware(const uint8_t *fw_data, uint32_t fw_size,
                            const uint8_t *nvram_data, uint32_t nvram_size);

/**
 * Bring up the WiFi interface
 * @return CYW_OK on success
 */
cyw_err_t cyw_up(void);

/**
 * Bring down the WiFi interface
 * @return CYW_OK on success
 */
cyw_err_t cyw_down(void);

/**
 * Send IOCTL command
 * @param cmd IOCTL command number
 * @param data Input/output data buffer
 * @param len Data length
 * @param set true for SET, false for GET
 * @return CYW_OK on success
 */
cyw_err_t cyw_ioctl(uint32_t cmd, void *data, uint32_t len, bool set);

/**
 * Get/set variable
 * @param name Variable name
 * @param data Input/output data buffer
 * @param len Data length
 * @param set true for SET, false for GET
 * @return CYW_OK on success
 */
cyw_err_t cyw_iovar(const char *name, void *data, uint32_t len, bool set);

/**
 * Scan for networks
 * @param results Buffer for scan results
 * @param max_results Maximum number of results
 * @return Number of networks found, or error
 */
int cyw_scan(void *results, int max_results);

/**
 * Connect to network
 * @param ssid SSID string
 * @param passphrase Passphrase (NULL for open network)
 * @return CYW_OK on success
 */
cyw_err_t cyw_connect(const char *ssid, const char *passphrase);

/**
 * Disconnect from network
 * @return CYW_OK on success
 */
cyw_err_t cyw_disconnect(void);

/**
 * Check if connected
 * @return true if connected
 */
bool cyw_is_connected(void);

/**
 * Get RSSI
 * @return RSSI value in dBm, or 0 on error
 */
int cyw_get_rssi(void);

/**
 * Process pending events (call from main loop or ISR)
 */
void cyw_poll(void);

/**
 * Get driver state
 * @return Current state
 */
cyw_state_t cyw_get_state(void);

/*============================================================================
 * Low-level SDIO Access (for debugging)
 *============================================================================*/

cyw_err_t cyw_sdio_read8(uint8_t func, uint32_t addr, uint8_t *val);
cyw_err_t cyw_sdio_write8(uint8_t func, uint32_t addr, uint8_t val);
cyw_err_t cyw_sdio_read32(uint32_t addr, uint32_t *val);
cyw_err_t cyw_sdio_write32(uint32_t addr, uint32_t val);
cyw_err_t cyw_backplane_read(uint32_t addr, uint8_t *data, uint32_t len);
cyw_err_t cyw_backplane_write(uint32_t addr, const uint8_t *data, uint32_t len);

#endif /* CYW55500_SDIO_H */
