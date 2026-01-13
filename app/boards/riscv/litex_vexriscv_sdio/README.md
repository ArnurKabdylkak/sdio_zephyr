# LiteX VexRiscv with SDIO - Zephyr Board

## Описание

Этот board definition описывает LiteX VexRiscv SoC с кастомным SDIO контроллером, развернутый на FPGA Sipeed Tang Primer 20K.

## Характеристики

- **CPU**: VexRiscv RV32IM @ 48 MHz
- **RAM**: 8 KB SRAM @ 0x10000000
- **Flash**: 128 KB ROM @ 0x00000000
- **UART**: LiteX UART @ 0xf0001000 (115200 baud)
- **Timer**: LiteX Timer @ 0xf0000800
- **SDIO**: Custom SDIO Controller @ 0x80000000
  - 4-bit bus width
  - Max frequency: 50 MHz
  - 2KB FIFO buffer
  - Hardware CRC7/CRC16

## SDIO Controller

### Адресная карта

| Offset  | Регистр                    | Описание |
|---------|----------------------------|----------|
| 0x0000  | MAIN_CLOCK_FREQ           | Частота системной шины (Hz) |
| 0x1000  | SD_CLOCK_FREQ             | Частота SDIO clock (Hz) |
| 0x2000  | CMD_INDEX                 | Индекс команды [5:0] |
| 0x3000  | CMD_ARGUMENT              | Аргумент / Ответ команды |
| 0x4000  | DATA_BUFFER               | Буфер данных (512x32bit) |
| 0x5000  | SEND_CMD_OP               | Операция: только команда |
| 0x6000  | SEND_CMD_READ_DATA_OP     | Операция: команда + чтение |
| 0x7000  | SEND_CMD_SEND_DATA_OP     | Операция: команда + запись |
| 0x8000  | READ_DATA_OP              | Операция: только чтение |
| 0x9000  | SEND_DATA_OP              | Операция: только запись |
| 0xA000  | CMD_BUSY                  | Флаг занятости CMD |
| 0xB000  | DATA_BUSY                 | Флаг занятости DATA |
| 0xC000  | CMD_STATUS                | Статус команды |
| 0xD000  | DATA_STATUS               | Статус данных |
| 0xE000  | DATA_LENGTH               | Длина данных (байты) |

### GPIO пины (Tang Primer 20K)

```
SDIO CLK  -> M14
SDIO CMD  -> M15
SDIO DAT0 -> J16
SDIO DAT1 -> J14
SDIO DAT2 -> R11
SDIO DAT3 -> T12
```

## Сборка и прошивка

### 1. Подготовка

Убедитесь, что LiteX SoC собран и прошит на FPGA:

```bash
cd litex/
python3 sipeed_tang_primer_20k.py --build
python3 sipeed_tang_primer_20k.py --load
```

### 2. Проверка адресов (опционально)

После сборки LiteX проверьте адреса:

```bash
cat litex/build/csr.csv
```

Если адреса изменились, обновите DTS:

```bash
./update_dts_from_litex.sh
```

### 3. Сборка Zephyr приложения

#### Вариант A: Используя этот board (рекомендуется)

```bash
cd app/
west build -b litex_vexriscv_sdio .
```

#### Вариант B: Используя BOARD_ROOT

Если board находится вне Zephyr tree:

```bash
export BOARD_ROOT=/home/arnurloq/sdio_zephyr/app
cd app/
west build -b litex_vexriscv_sdio .
```

### 4. Загрузка на FPGA

#### Через litex_term (рекомендуется)

```bash
litex_term --kernel build/zephyr/zephyr.bin /dev/ttyUSB1
```

#### Через UART bootloader

```bash
# Если у вас настроен UART bootloader в LiteX
west flash
```

## Использование SDIO HAL

В вашем приложении:

```c
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>

// SDIO base address from Device Tree
#define SDIO_NODE DT_NODELABEL(sdio0)
#define SDIO_BASE DT_REG_ADDR(SDIO_NODE)

// Или напрямую (если не используете DT API)
#define SDIO_BASE 0x80000000

#include "sdio_hal.h"

int main(void) {
    // Инициализация SDIO HAL
    sdio_init(48000000, 100000);

    // Отправка команды
    sdio_response_t resp;
    sdio_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, &resp);

    return 0;
}
```

## Пример приложения

См. `app/src/main_litex.c` для полного примера инициализации SDIO WiFi модуля.

## Отладка

### Последовательный порт

Подключите USB-UART адаптер к FPGA:
- TX -> UART RX pin
- RX -> UART TX pin
- GND -> GND

Откройте терминал:

```bash
picocom /dev/ttyUSB0 -b 115200
# или
litex_term /dev/ttyUSB0
```

### Проверка Device Tree

Посмотрите финальный скомпилированный DTS:

```bash
cat build/zephyr/zephyr.dts | grep -A 30 "sdio@"
```

### Проверка памяти

```bash
riscv64-unknown-elf-nm build/zephyr/zephyr.elf | grep sdio
riscv64-unknown-elf-objdump -h build/zephyr/zephyr.elf
```

## Известные ограничения

1. **Размер SRAM**: Только 8 KB - будьте аккуратны с stack/heap
2. **Нет interrupts**: SDIO работает в polling режиме
3. **Нет DMA**: Все передачи данных через CPU
4. **Максимальный transfer**: 2048 байт (размер буфера)

## Обновление после изменений LiteX

Если вы изменили конфигурацию LiteX (добавили периферию, изменили адреса):

```bash
# 1. Пересобрать LiteX
cd litex/
python3 sipeed_tang_primer_20k.py --build --csr-csv=build/csr.csv

# 2. Обновить DTS
cd ..
./update_dts_from_litex.sh

# 3. Пересобрать Zephyr
cd app/
west build -t clean
west build -b litex_vexriscv_sdio .
```

## Полезные ссылки

- [LiteX Documentation](https://github.com/enjoy-digital/litex)
- [Zephyr RISCV](https://docs.zephyrproject.org/latest/boards/riscv/index.html)
- [VexRiscv CPU](https://github.com/SpinalHDL/VexRiscv)
- [Sipeed Tang Primer 20K](https://wiki.sipeed.com/hardware/en/tang/tang-primer-20k/primer-20k.html)

## Файлы board definition

```
boards/riscv/litex_vexriscv_sdio/
├── README.md                           # Этот файл
├── litex_vexriscv_sdio.dts            # Device Tree Source
├── litex_vexriscv_sdio_defconfig      # Default config
├── board.cmake                         # Build configuration
├── board.yml                           # Board metadata
├── Kconfig.board                       # Kconfig entry
└── Kconfig.defconfig                   # Default Kconfig values
```
