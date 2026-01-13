/**
 * CYW55500 WiFi Chip - Register Definitions
 * Bare-metal driver for RISC-V SoC
 *
 * Based on brcmfmac Linux driver
 * License: MIT / GPL-2.0 compatible
 */

#ifndef CYW55500_REGS_H
#define CYW55500_REGS_H

#include <stdint.h>

/*============================================================================
 * Chip Identification
 *============================================================================*/

#define CYW55500_CHIP_ID            0xD8CC
#define CYW55500_CHIP_ID_MASK       0x0000FFFF
#define CYW55500_CHIP_REV_MASK      0x000F0000
#define CYW55500_CHIP_REV_SHIFT     16

/*============================================================================
 * SDIO Function Definitions
 *============================================================================*/

#define SDIO_FUNC_0                 0   /* CCCR/FBR */
#define SDIO_FUNC_1                 1   /* Backplane */
#define SDIO_FUNC_2                 2   /* WLAN Data */
#define SDIO_FUNC_3                 3   /* Bluetooth (if available) */

/* Function enable bits */
#define SDIO_FUNC_ENABLE_1          0x02
#define SDIO_FUNC_ENABLE_2          0x04
#define SDIO_FUNC_READY_1           0x02
#define SDIO_FUNC_READY_2           0x04

/*============================================================================
 * SDIO CCCR Registers (Function 0)
 *============================================================================*/

#define CCCR_SDIO_REVISION          0x00
#define CCCR_SD_SPEC                0x01
#define CCCR_IO_ENABLE              0x02
#define CCCR_IO_READY               0x03
#define CCCR_INT_ENABLE             0x04
#define CCCR_INT_PENDING            0x05
#define CCCR_IO_ABORT               0x06
#define CCCR_BUS_IF_CTRL            0x07
#define CCCR_CARD_CAPS              0x08
#define CCCR_CIS_PTR                0x09    /* 3 bytes */
#define CCCR_BUS_SUSPEND            0x0C
#define CCCR_FUNC_SELECT            0x0D
#define CCCR_EXEC_FLAGS             0x0E
#define CCCR_READY_FLAGS            0x0F
#define CCCR_FN0_BLKSIZE            0x10    /* 2 bytes */
#define CCCR_POWER_CTRL             0x12
#define CCCR_HS_SPEED               0x13
#define CCCR_UHS_SUPPORT            0x14
#define CCCR_DRIVE_STRENGTH         0x15
#define CCCR_INT_EXT                0x16

/* Broadcom/Infineon vendor CCCR registers */
#define SDIO_CCCR_BRCM_CARDCAP      0xF0
#define SDIO_CCCR_BRCM_CARDCTRL     0xF1
#define SDIO_CCCR_BRCM_SEPINT       0xF2

/* BRCM_CARDCAP bits */
#define BRCM_CARDCAP_CMD14_SUPPORT  (1 << 1)
#define BRCM_CARDCAP_CMD14_EXT      (1 << 2)
#define BRCM_CARDCAP_CMD_NODEC      (1 << 3)
#define BRCM_CARDCAP_CHIPID_PRESENT (1 << 6)
#define BRCM_CARDCAP_SECURE_MODE    (1 << 7)

/* BRCM_CARDCTRL bits */
#define BRCM_CARDCTRL_WLANRESET     (1 << 1)
#define BRCM_CARDCTRL_BTRESET       (1 << 2)

/* BRCM_SEPINT bits */
#define BRCM_SEPINT_MASK            (1 << 0)
#define BRCM_SEPINT_OE              (1 << 1)
#define BRCM_SEPINT_ACT_HI          (1 << 2)

/* Interrupt enable bits */
#define CCCR_IEN_FUNC0              (1 << 0)
#define CCCR_IEN_FUNC1              (1 << 1)
#define CCCR_IEN_FUNC2              (1 << 2)

/*============================================================================
 * SDIO Function 1 Misc Registers (0x10000 - 0x1001F)
 *============================================================================*/

#define SBSDIO_FUNC1_MISC_REG_START 0x10000
#define SBSDIO_FUNC1_MISC_REG_LIMIT 0x1001F

#define SBSDIO_SPROM_CS             0x10000
#define SBSDIO_SPROM_INFO           0x10001
#define SBSDIO_SPROM_DATA_LOW       0x10002
#define SBSDIO_SPROM_DATA_HIGH      0x10003
#define SBSDIO_SPROM_ADDR_LOW       0x10004
#define SBSDIO_GPIO_SELECT          0x10005
#define SBSDIO_GPIO_OUT             0x10006
#define SBSDIO_GPIO_EN              0x10007
#define SBSDIO_WATERMARK            0x10008
#define SBSDIO_DEVICE_CTL           0x10009
#define SBSDIO_FUNC1_SBADDRLOW      0x1000A
#define SBSDIO_FUNC1_SBADDRMID      0x1000B
#define SBSDIO_FUNC1_SBADDRHIGH     0x1000C
#define SBSDIO_FUNC1_FRAMECTRL      0x1000D
#define SBSDIO_FUNC1_CHIPCLKCSR     0x1000E
#define SBSDIO_FUNC1_SDIOPULLUP     0x1000F
#define SBSDIO_FUNC1_WFRAMEBCLO     0x10019
#define SBSDIO_FUNC1_WFRAMEBCHI     0x1001A
#define SBSDIO_FUNC1_RFRAMEBCLO     0x1001B
#define SBSDIO_FUNC1_RFRAMEBCHI     0x1001C
#define SBSDIO_FUNC1_MESBUSYCTRL    0x1001D
#define SBSDIO_FUNC1_WAKEUPCTRL     0x1001E
#define SBSDIO_FUNC1_SLEEPCSR       0x1001F

/* Secure mode register (rev27+) */
#define SBSDIO_FUNC1_SECURE_MODE    0x10001

/* DEVICE_CTL bits */
#define SBSDIO_DEVCTL_SETBUSY       0x01
#define SBSDIO_DEVCTL_SPI_INTR_SYNC 0x02
#define SBSDIO_DEVCTL_CA_INT_ONLY   0x04
#define SBSDIO_DEVCTL_PADS_ISO      0x08
#define SBSDIO_DEVCTL_F2WM_ENAB     0x10
#define SBSDIO_DEVCTL_SB_RST_CTL    0x30
#define SBSDIO_DEVCTL_RST_CORECTL   0x00
#define SBSDIO_DEVCTL_RST_BPRESET   0x10
#define SBSDIO_DEVCTL_RST_NOBPRESET 0x20
#define SBSDIO_DEVCTL_ADDR_RESET    0x40

/* CHIPCLKCSR bits */
#define SBSDIO_FORCE_ALP            0x01
#define SBSDIO_FORCE_HT             0x02
#define SBSDIO_FORCE_ILP            0x04
#define SBSDIO_ALP_AVAIL_REQ        0x08
#define SBSDIO_HT_AVAIL_REQ         0x10
#define SBSDIO_FORCE_HW_CLKREQ_OFF  0x20
#define SBSDIO_ALP_AVAIL            0x40
#define SBSDIO_HT_AVAIL             0x80

/* SLEEPCSR bits */
#define SBSDIO_SLEEPCSR_KSO         0x01    /* Keep SDIO On */
#define SBSDIO_SLEEPCSR_DEVON       0x02    /* Device On */

/* MESBUSYCTRL bits */
#define SBSDIO_MESBUSY_RXFIFO_WM_MASK   0x7F
#define SBSDIO_MESBUSYCTRL_ENAB         0x80

/* Backplane window */
#define SBSDIO_SB_OFT_ADDR_MASK     0x07FFF
#define SBSDIO_SB_OFT_ADDR_LIMIT    0x08000
#define SBSDIO_SB_ACCESS_2_4B_FLAG  0x08000
#define SBSDIO_SBWINDOW_MASK        0xFFFF8000

/*============================================================================
 * SDIO Core Registers
 *============================================================================*/

/* Core control */
#define SDIO_CORE_CORECONTROL       0x000
#define SDIO_CORE_CORESTATUS        0x004
#define SDIO_CORE_BISTSTATUS        0x00C

/* Interrupt registers */
#define SDIO_CORE_INTSTATUS         0x020
#define SDIO_CORE_HOSTINTMASK       0x024
#define SDIO_CORE_INTMASK           0x028
#define SDIO_CORE_SBINTSTATUS       0x02C
#define SDIO_CORE_SBINTMASK         0x030
#define SDIO_CORE_FUNCINTMASK       0x034

/* Mailbox registers */
#define SDIO_CORE_TOSBMAILBOX       0x040
#define SDIO_CORE_TOHOSTMAILBOX     0x044
#define SDIO_CORE_TOSBMAILBOXDATA   0x048
#define SDIO_CORE_TOHOSTMAILBOXDATA 0x04C

/* SDIO access */
#define SDIO_CORE_SDIOACCESS        0x050

/* Lazy interrupt */
#define SDIO_CORE_INTRCVLAZY        0x100

/* Counters */
#define SDIO_CORE_CMD52RD           0x110
#define SDIO_CORE_CMD52WR           0x114
#define SDIO_CORE_CMD53RD           0x118
#define SDIO_CORE_CMD53WR           0x11C
#define SDIO_CORE_ABORT             0x120
#define SDIO_CORE_DATACRCERROR      0x124

/* Clock control */
#define SDIO_CORE_CLOCKCTLSTATUS    0x1E0

/* Chip ID (rev31+) */
#define SDIO_CORE_CHIPID            0x330
#define SDIO_CORE_EROMPTR           0x334

/* Interrupt status bits */
#define I_SMB_SW_MASK               0x0000000F
#define I_HMB_SW_MASK               0x000000F0
#define I_HMB_FC_CHANGE             (1 << 8)
#define I_HMB_FRAME_IND             (1 << 9)
#define I_HMB_HOST_INT              (1 << 10)

/* Host mailbox data bits */
#define HMB_DATA_NAKHANDLED         0x0001
#define HMB_DATA_DEVREADY           0x0002
#define HMB_DATA_FC                 0x0004
#define HMB_DATA_FWREADY            0x0008
#define HMB_DATA_FWHALT             0x0010
#define HMB_DATA_FCDATA_MASK        0xFF000000
#define HMB_DATA_FCDATA_SHIFT       24
#define HMB_DATA_VERSION_MASK       0x00FF0000
#define HMB_DATA_VERSION_SHIFT      16

/*============================================================================
 * ChipCommon Registers
 *============================================================================*/

#define CC_CHIPID                   0x000
#define CC_CAPABILITIES             0x004
#define CC_CORECONTROL              0x008
#define CC_BIST                     0x00C
#define CC_OTPSTATUS                0x010
#define CC_OTPCONTROL               0x014
#define CC_OTPPROG                  0x018
#define CC_OTPLAYOUT                0x01C
#define CC_INTSTATUS                0x020
#define CC_INTMASK                  0x024
#define CC_CHIPCONTROL              0x028
#define CC_CHIPSTATUS               0x02C
#define CC_GPIOPULLUP               0x058
#define CC_GPIOPULLDOWN             0x05C
#define CC_GPIOIN                   0x060
#define CC_GPIOOUT                  0x064
#define CC_GPIOOUTEN                0x068
#define CC_GPIOCONTROL              0x06C
#define CC_WATCHDOG                 0x080
#define CC_CLKDIV                   0x0A4
#define CC_CAPABILITIES_EXT         0x0AC
#define CC_EROMPTR                  0x0FC
#define CC_CLK_CTL_ST               0x1E0

/* Capabilities bits */
#define CC_CAP_PMU                  (1 << 28)
#define CC_CAP_SROM                 (1 << 30)

/*============================================================================
 * PMU Registers (offset from ChipCommon base + 0x600)
 *============================================================================*/

#define PMU_BASE_OFFSET             0x600

#define PMU_CONTROL                 0x000
#define PMU_CAPABILITIES            0x004
#define PMU_STATUS                  0x008
#define PMU_RES_STATE               0x00C
#define PMU_RES_PENDING             0x010
#define PMU_TIMER                   0x014
#define PMU_MIN_RES_MASK            0x018
#define PMU_MAX_RES_MASK            0x01C
#define PMU_RES_TABLE_SEL           0x020
#define PMU_RES_DEP_MASK            0x024
#define PMU_RES_UPDN_TIMER          0x028
#define PMU_RES_TIMER               0x02C
#define PMU_CLKSTRETCH              0x030
#define PMU_WATCHDOG                0x034
#define PMU_GPIOSEL                 0x038
#define PMU_GPIOENABLE              0x03C
#define PMU_RES_REQ_TIMER_SEL       0x040
#define PMU_RES_REQ_TIMER           0x044
#define PMU_RES_REQ_MASK            0x048
#define PMU_CAPABILITIES_EXT        0x04C
#define PMU_CHIPCONTROL_ADDR        0x050
#define PMU_CHIPCONTROL_DATA        0x054
#define PMU_REGCONTROL_ADDR         0x058
#define PMU_REGCONTROL_DATA         0x05C
#define PMU_PLLCONTROL_ADDR         0x060
#define PMU_PLLCONTROL_DATA         0x064
#define PMU_STRAPOPT                0x068
#define PMU_XTALFREQ                0x06C
#define PMU_RETENTION_CTL           0x070

/* PMU capabilities bits */
#define PCAP_REV_MASK               0x000000FF
#define PCAP_RC_MASK                0x00001F00
#define PCAP_RC_SHIFT               8

/*============================================================================
 * BCMA Core IDs
 *============================================================================*/

#define BCMA_CORE_CHIPCOMMON        0x800
#define BCMA_CORE_80211             0x812
#define BCMA_CORE_SDIO_DEV          0x829
#define BCMA_CORE_ARM_CR4           0x83E
#define BCMA_CORE_PMU               0x827
#define BCMA_CORE_GCI               0x840
#define BCMA_CORE_SYS_MEM           0x849

#define BCMA_CORE_SIZE              0x1000

/*============================================================================
 * ARM Core Registers
 *============================================================================*/

#define ARMCR4_CAP                  0x04
#define ARMCR4_BANKIDX              0x40
#define ARMCR4_BANKINFO             0x44
#define ARMCR4_BANKPDA              0x4C

#define ARMCR4_TCBBNB_MASK          0xF0
#define ARMCR4_TCBANB_MASK          0x0F
#define ARMCR4_BSZ_MASK             0x3F

/*============================================================================
 * D11 MAC Core Registers
 *============================================================================*/

#define D11_BASE_ADDR               0x18001000
#define D11_AXI_BASE_ADDR           0xE8000000
#define D11_SHM_BASE_ADDR           (D11_AXI_BASE_ADDR + 0x4000)

#define D11_MACCONTROL              0x120
#define D11_MACCONTROL_WAKE         (1 << 26)

/*============================================================================
 * CYW55500 Specific Parameters
 *============================================================================*/

#define CYW55500_RAM_START          0x3A0000
#define CYW55500_TCAM_SIZE          0x800
#define CYW55500_TRXHDR_SIZE        0x2B4

#define CYW55500_A1_TCAM_SIZE       0x1000
#define CYW55500_A1_TRXHDR_SIZE     0x20

/* Firmware download addresses */
#define TRX_HDR_START_ADDR          0x7FD4C
#define TRX_HDR_SIZE                0x2B4
#define NVRAM_DL_ADDR               0x80000
#define CM3_SOCRAM_WRITE_END        0x80000

/* DAR registers */
#define BRCMF_SDIO_REG_DAR_H2D_MSG_0    0x10030
#define BRCMF_SDIO_REG_DAR_D2H_MSG_0    0x10038
#define BRCMF_SDIO_REG_H2D_MSG_0        0x18002048
#define BRCMF_SDIO_REG_D2H_MSG_0        0x1800204C

/* Watermark values */
#define CYW55500_F2_WATERMARK       0x40
#define CYW55500_MES_WATERMARK      0x40

/*============================================================================
 * SDPCM Protocol Definitions
 *============================================================================*/

#define SDPCM_SHARED_VERSION        0x0003
#define SDPCM_SHARED_VERSION_MASK   0x00FF
#define SDPCM_SHARED_ASSERT_BUILT   0x0100
#define SDPCM_SHARED_ASSERT         0x0200
#define SDPCM_SHARED_TRAP           0x0400

/* SDPCM header fields */
#define SDPCM_SEQ_MASK              0x000000FF
#define SDPCM_CHANNEL_MASK          0x00000F00
#define SDPCM_CHANNEL_SHIFT         8
#define SDPCM_NEXTLEN_MASK          0x00FF0000
#define SDPCM_NEXTLEN_SHIFT         16
#define SDPCM_DOFFSET_MASK          0xFF000000
#define SDPCM_DOFFSET_SHIFT         24

/* SDPCM channels */
#define SDPCM_CONTROL_CHANNEL       0
#define SDPCM_EVENT_CHANNEL         1
#define SDPCM_DATA_CHANNEL          2
#define SDPCM_GLOM_CHANNEL          3

/*============================================================================
 * BCDC Protocol Definitions
 *============================================================================*/

#define BCDC_HEADER_LEN             4
#define BCDC_PROTO_VER              2

/* BCDC flags */
#define BCDC_FLAG_VER_MASK          0xF0
#define BCDC_FLAG_VER_SHIFT         4
#define BCDC_FLAG_SUM_GOOD          0x04
#define BCDC_FLAG_SUM_NEEDED        0x08

/* BCDC flags2 */
#define BCDC_FLAG2_IF_MASK          0x0F

/*============================================================================
 * Firmware IOCTLs
 *============================================================================*/

/* Basic IOCTLs */
#define WLC_GET_MAGIC               0
#define WLC_GET_VERSION             1
#define WLC_UP                      2
#define WLC_DOWN                    3
#define WLC_GET_VAR                 262
#define WLC_SET_VAR                 263

/* WiFi IOCTLs */
#define WLC_SET_SSID                26
#define WLC_GET_SSID                25
#define WLC_SET_CHANNEL             30
#define WLC_GET_CHANNEL             29
#define WLC_SET_AP                  118
#define WLC_GET_AP                  117
#define WLC_SET_INFRA               20
#define WLC_GET_INFRA               19
#define WLC_SET_AUTH                22
#define WLC_GET_AUTH                21
#define WLC_SET_WSEC                134
#define WLC_GET_WSEC                133
#define WLC_SET_WPA_AUTH            165
#define WLC_GET_WPA_AUTH            164
#define WLC_SET_KEY                 45
#define WLC_SCAN                    50
#define WLC_SCAN_RESULTS            51
#define WLC_DISASSOC                52
#define WLC_REASSOC                 53
#define WLC_GET_RSSI                127
#define WLC_GET_BSSID               23

/*============================================================================
 * Utility Macros
 *============================================================================*/

#define ALIGN(x, a)                 (((x) + (a) - 1) & ~((a) - 1))
#define ARRAY_SIZE(x)               (sizeof(x) / sizeof((x)[0]))
#define BIT(n)                      (1U << (n))

#define REG32(addr)                 (*(volatile uint32_t *)(addr))
#define REG16(addr)                 (*(volatile uint16_t *)(addr))
#define REG8(addr)                  (*(volatile uint8_t *)(addr))

#endif /* CYW55500_REGS_H */
