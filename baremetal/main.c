/**
 * CYW55500 WiFi - Bare-metal Example for RISC-V
 *
 * Example usage of the WiFi driver
 */

#include <stdint.h>
#include <stdbool.h>
#include "baremetal.h"
#include "cyw55500_sdio.h"
#include "sdio_litex.h"

/*============================================================================
 * Firmware Data
 *
 * In a real application, these would be:
 * 1. Loaded from flash/SD card
 * 2. Embedded as binary blobs using objcopy
 * 3. Fetched via bootloader
 *============================================================================*/

/* Example: Include firmware as binary blob */
/* These symbols are defined by the linker when including binary files */
extern const uint8_t _binary_cyfmac55500_sdio_bin_start[];
extern const uint8_t _binary_cyfmac55500_sdio_bin_end[];
extern const uint8_t _binary_cyfmac55500_sdio_txt_start[];
extern const uint8_t _binary_cyfmac55500_sdio_txt_end[];

/* Alternatively, for testing without real firmware: */
#if 0
static const uint8_t dummy_fw[] = { /* ... */ };
static const uint8_t dummy_nvram[] = "# NVRAM\nboardtype=0xffff\n";
#endif

/*============================================================================
 * Console Output (Platform Specific)
 *============================================================================*/

/* TODO: Implement based on your UART */
extern void uart_puts(const char *s);
extern void uart_puthex(uint32_t val);

static void print(const char *msg)
{
#ifdef UART_AVAILABLE
    uart_puts(msg);
#else
    (void)msg;
#endif
}

static void print_hex(const char *prefix, uint32_t val)
{
#ifdef UART_AVAILABLE
    uart_puts(prefix);
    uart_puthex(val);
    uart_puts("\n");
#else
    (void)prefix;
    (void)val;
#endif
}

/*============================================================================
 * WiFi Event Callback (Optional)
 *============================================================================*/

void wifi_event_handler(uint32_t event_type, void *data, uint32_t len)
{
    (void)data;
    (void)len;

    switch (event_type) {
        case 0: /* Link up */
            print("WiFi: Link Up\n");
            break;
        case 1: /* Link down */
            print("WiFi: Link Down\n");
            break;
        case 16: /* Scan complete */
            print("WiFi: Scan Complete\n");
            break;
        default:
            print_hex("WiFi Event: ", event_type);
            break;
    }
}

/*============================================================================
 * Main Application
 *============================================================================*/

int main(void)
{
    cyw_err_t err;
    cyw_chip_info_t chip_info;

    print("\n=== CYW55500 WiFi Bare-metal Driver ===\n\n");

    /*------------------------------------------------------------------------
     * Step 1: Initialize the driver
     *------------------------------------------------------------------------*/
    print("Initializing WiFi driver...\n");

    err = cyw_init(litex_get_sdio_ops());
    if (err != CYW_OK) {
        print("ERROR: Driver init failed\n");
        print_hex("Error code: ", err);
        goto error;
    }

    print("Driver initialized OK\n");

    /*------------------------------------------------------------------------
     * Step 2: Get chip information
     *------------------------------------------------------------------------*/
    err = cyw_get_chip_info(&chip_info);
    if (err == CYW_OK) {
        print_hex("Chip ID: ", chip_info.chip_id);
        print_hex("Chip Rev: ", chip_info.chip_rev);
        print_hex("RAM Base: ", chip_info.ram_base);
    }

    /*------------------------------------------------------------------------
     * Step 3: Load firmware
     *------------------------------------------------------------------------*/
    print("Loading firmware...\n");

#ifdef USE_EMBEDDED_FW
    /* Use embedded firmware */
    uint32_t fw_size = _binary_cyfmac55500_sdio_bin_end -
                       _binary_cyfmac55500_sdio_bin_start;
    uint32_t nvram_size = _binary_cyfmac55500_sdio_txt_end -
                          _binary_cyfmac55500_sdio_txt_start;

    err = cyw_load_firmware(_binary_cyfmac55500_sdio_bin_start, fw_size,
                            _binary_cyfmac55500_sdio_txt_start, nvram_size);
#else
    /* For testing - you need to provide actual firmware data */
    print("WARNING: No firmware embedded. Skipping FW load.\n");
    err = CYW_ERR_FW;
#endif

    if (err != CYW_OK) {
        print("ERROR: Firmware load failed\n");
        print_hex("Error code: ", err);
        /* Continue anyway for testing register access */
    } else {
        print("Firmware loaded OK\n");
    }

    /*------------------------------------------------------------------------
     * Step 4: Bring up WiFi interface
     *------------------------------------------------------------------------*/
    if (cyw_get_state() >= CYW_STATE_FW_READY) {
        print("Bringing up WiFi interface...\n");

        err = cyw_up();
        if (err != CYW_OK) {
            print("ERROR: WiFi UP failed\n");
            goto error;
        }

        print("WiFi interface is UP\n");
    }

    /*------------------------------------------------------------------------
     * Step 5: Example operations
     *------------------------------------------------------------------------*/
    if (cyw_get_state() >= CYW_STATE_UP) {
        /* Get MAC address */
        uint8_t mac[6];
        err = cyw_iovar("cur_etheraddr", mac, 6, false);
        if (err == CYW_OK) {
            print("MAC Address: ");
            for (int i = 0; i < 6; i++) {
                print_hex("", mac[i]);
                if (i < 5) print(":");
            }
            print("\n");
        }

        /* Get firmware version */
        char ver[64] = {0};
        err = cyw_iovar("ver", ver, sizeof(ver) - 1, false);
        if (err == CYW_OK) {
            print("Firmware: ");
            print(ver);
            print("\n");
        }

        /* Set country code */
        struct {
            char ccode[4];
            uint32_t rev;
        } country = { "KZ", 0 };
        cyw_iovar("country", &country, sizeof(country), true);

        /* Example: Start scan */
        print("Starting WiFi scan...\n");
        /* TODO: Implement scan and result retrieval */

        /* Example: Connect to network */
        /*
        print("Connecting to network...\n");
        err = cyw_connect("MyNetwork", "MyPassword");
        if (err == CYW_OK) {
            print("Connected!\n");

            int rssi = cyw_get_rssi();
            print_hex("RSSI: ", rssi);
        }
        */
    }

    /*------------------------------------------------------------------------
     * Step 6: Main loop
     *------------------------------------------------------------------------*/
    print("\nEntering main loop...\n");

    while (1) {
        /* Poll for events */
        cyw_poll();

        /* Your application code here */

        /* Simple delay */
        for (volatile int i = 0; i < 100000; i++);
    }

    return 0;

error:
    print("\nDriver initialization failed!\n");
    cyw_deinit();

    while (1) {
        /* Halt */
    }

    return -1;
}

/*============================================================================
 * IRQ Handler (if using interrupts)
 *============================================================================*/

void sdio_irq_handler(void)
{
    /* Called from interrupt context */
    cyw_poll();
}
