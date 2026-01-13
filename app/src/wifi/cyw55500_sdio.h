/**
 * CYW55500 WiFi - SDIO Driver Interface
 * Zephyr RTOS port for RP2350
 */

#ifndef CYW55500_SDIO_H
#define CYW55500_SDIO_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>
#include "cyw55500_regs.h"

/*============================================================================
 * Configuration
 *============================================================================*/

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
 * Scan Results
 *============================================================================*/

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[33];
    uint8_t ssid_len;
    int16_t rssi;
    uint16_t channel;
    uint8_t security;
} cyw_scan_result_t;

#define CYW_SEC_OPEN        0
#define CYW_SEC_WEP         1
#define CYW_SEC_WPA_PSK     2
#define CYW_SEC_WPA2_PSK    3
#define CYW_SEC_WPA3_SAE    4

/*============================================================================
 * SDPCM Header
 *============================================================================*/

typedef struct __attribute__((packed)) {
    uint16_t len;
    uint16_t len_check;
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
 *============================================================================*/

typedef struct {
    int (*init)(void);
    void (*deinit)(void);
    int (*cmd52_read)(uint8_t func, uint32_t addr, uint8_t *val);
    int (*cmd52_write)(uint8_t func, uint32_t addr, uint8_t val);
    int (*cmd53_read)(uint8_t func, uint32_t addr, uint8_t *data,
                      uint32_t len, bool incr_addr);
    int (*cmd53_write)(uint8_t func, uint32_t addr, const uint8_t *data,
                       uint32_t len, bool incr_addr);
    int (*set_block_size)(uint8_t func, uint16_t block_size);
    int (*enable_func)(uint8_t func, bool enable);
    int (*enable_irq)(bool enable);
    bool (*irq_pending)(void);
    void (*delay_us)(uint32_t us);
    void (*delay_ms)(uint32_t ms);
} sdio_host_ops_t;

/*============================================================================
 * Public API
 *============================================================================*/

cyw_err_t cyw_init(const sdio_host_ops_t *ops);
void cyw_deinit(void);

cyw_err_t cyw_get_chip_info(cyw_chip_info_t *info);
cyw_err_t cyw_load_firmware(const uint8_t *fw_data, uint32_t fw_size,
                            const uint8_t *nvram_data, uint32_t nvram_size);

cyw_err_t cyw_up(void);
cyw_err_t cyw_down(void);

cyw_err_t cyw_ioctl(uint32_t cmd, void *data, uint32_t len, bool set);
cyw_err_t cyw_iovar(const char *name, void *data, uint32_t len, bool set);

int cyw_scan(cyw_scan_result_t *results, int max_results);
cyw_err_t cyw_connect(const char *ssid, const char *passphrase);
cyw_err_t cyw_disconnect(void);
bool cyw_is_connected(void);
int cyw_get_rssi(void);

void cyw_poll(void);
cyw_state_t cyw_get_state(void);

/*============================================================================
 * Low-level SDIO Access
 *============================================================================*/

cyw_err_t cyw_sdio_read8(uint8_t func, uint32_t addr, uint8_t *val);
cyw_err_t cyw_sdio_write8(uint8_t func, uint32_t addr, uint8_t val);
cyw_err_t cyw_sdio_read32(uint32_t addr, uint32_t *val);
cyw_err_t cyw_sdio_write32(uint32_t addr, uint32_t val);
cyw_err_t cyw_backplane_read(uint32_t addr, uint8_t *data, uint32_t len);
cyw_err_t cyw_backplane_write(uint32_t addr, const uint8_t *data, uint32_t len);

#endif /* CYW55500_SDIO_H */
