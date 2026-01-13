/**
 * CYW55500 Firmware Blob
 *
 * This is a placeholder file. Replace with actual firmware data.
 *
 * To generate from binary file:
 *   xxd -i cyw55500_fw.bin > fw_data.h
 *
 * Or use Python:
 *   with open('cyw55500_fw.bin', 'rb') as f:
 *       data = f.read()
 *   print('const uint8_t cyw55500_fw[] = {')
 *   for i, b in enumerate(data):
 *       print(f'0x{b:02x},', end=' ' if (i+1)%16 else '\n')
 *   print('};')
 */

#include <stdint.h>

/* Placeholder firmware - replace with actual CYW55500 firmware */
const uint8_t cyw55500_fw[] = {
    /* TODO: Insert actual firmware bytes here */
    0x00
};
const uint32_t cyw55500_fw_len = sizeof(cyw55500_fw);

/* Placeholder NVRAM - replace with actual NVRAM data */
/* NVRAM contains calibration data and configuration for the WiFi chip */
const uint8_t cyw55500_nvram[] = {
    /* Typical NVRAM format (null-terminated key=value pairs):
     * "manfid=0x2d0"
     * "prodid=0x0726"
     * "vendid=0x14e4"
     * "devid=0x43e2"
     * "boardtype=0x0726"
     * "boardrev=0x1100"
     * "boardnum=22"
     * "macaddr=00:90:4c:c5:12:38"
     * "sromrev=11"
     * "boardflags=0x00000400"
     * "xtalfreq=37400"
     * etc...
     */
    /* TODO: Insert actual NVRAM data here */
    0x00
};
const uint32_t cyw55500_nvram_len = sizeof(cyw55500_nvram);
