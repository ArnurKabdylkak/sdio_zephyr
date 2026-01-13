# ✅ Создан полноценный .dts файл для LiteX VexRiscv + Zephyr

## Что создано

### 🎯 Главный файл: Device Tree Source

**Файл:** `app/boards/riscv/litex_vexriscv_sdio/litex_vexriscv_sdio.dts`

Полное описание SoC в формате Device Tree, включающее:

#### 1. CPU Configuration
```dts
cpu0: cpu@0 {
    compatible = "riscv";
    riscv,isa = "rv32im";
    clock-frequency = <48000000>;
}
```

#### 2. Memory Regions
```dts
flash0: flash@0 {
    reg = <0x00000000 0x20000>;  // 128 KB ROM
}

sram0: memory@10000000 {
    reg = <0x10000000 0x2000>;   // 8 KB RAM
}
```

#### 3. Peripherals
```dts
uart0: serial@f0001000 {
    compatible = "litex,uart0";
    reg = <0xf0001000 0x100>;
}

timer0: timer@f0000800 {
    compatible = "litex,timer0";
    reg = <0xf0000800 0x40>;
}
```

#### 4. SDIO Controller (главная часть!)
```dts
sdio0: sdio@80000000 {
    compatible = "litex,sdio";
    reg = <0x80000000 0x10000>;
    
    /* Hardware capabilities */
    max-frequency = <50000000>;
    clock-frequency = <48000000>;
    bus-width = <4>;
    fifo-depth = <512>;
    
    /* Register offsets - все 15 регистров */
    litex,reg-main-clk-freq = <0x0000>;
    litex,reg-sd-clk-freq = <0x1000>;
    litex,reg-cmd-index = <0x2000>;
    litex,reg-cmd-argument = <0x3000>;
    litex,reg-data-buffer = <0x4000>;
    litex,reg-send-cmd-op = <0x5000>;
    litex,reg-send-cmd-read-data-op = <0x6000>;
    litex,reg-send-cmd-send-data-op = <0x7000>;
    litex,reg-read-data-op = <0x8000>;
    litex,reg-send-data-op = <0x9000>;
    litex,reg-cmd-busy = <0xA000>;
    litex,reg-data-busy = <0xB000>;
    litex,reg-cmd-status = <0xC000>;
    litex,reg-data-status = <0xD000>;
    litex,reg-data-length = <0xE000>;
}
```

## Полная структура board

```
app/boards/riscv/litex_vexriscv_sdio/
├── litex_vexriscv_sdio.dts            ⭐ Device Tree (197 строк)
├── litex_vexriscv_sdio_defconfig      ← Zephyr config
├── board.cmake                         ← Flash/Load скрипты
├── board.yml                           ← Board metadata
├── Kconfig.board                       ← Kconfig integration
├── Kconfig.defconfig                   ← Default Kconfig
└── README.md                           ← Board документация
```

## Вспомогательные файлы

```
update_dts_from_litex.sh               ← Автообновление адресов из LiteX
HOWTO_USE_DTS.md                       ← Инструкция по использованию
```

## Как использовать

### Вариант 1: Полноценный board (рекомендуется)

```bash
export BOARD_ROOT=/home/arnurloq/sdio_zephyr/app
cd app/
west build -b litex_vexriscv_sdio .
```

### Вариант 2: Только .dts как overlay

```bash
west build -b litex_vexriscv . -- \
    -DDTC_OVERLAY_FILE="boards/riscv/litex_vexriscv_sdio/litex_vexriscv_sdio.dts"
```

## Адреса в DTS

| Компонент | Адрес | Размер | Комментарий |
|-----------|-------|--------|-------------|
| ROM/Flash | 0x00000000 | 128 KB | Bootloader |
| SRAM | 0x10000000 | 8 KB | Main RAM |
| UART | 0xf0001000 | 256 B | Проверить из LiteX |
| Timer | 0xf0000800 | 64 B | Проверить из LiteX |
| **SDIO** | **0x80000000** | **64 KB** | **Фиксирован** |

**ВАЖНО:** Адреса UART и Timer могут измениться при пересборке LiteX!
Адрес SDIO (0x80000000) фиксирован в sipeed_tang_primer_20k.py:247.

## Проверка после сборки LiteX

```bash
# 1. Соберите LiteX
cd litex/
python3 sipeed_tang_primer_20k.py --build --csr-csv=build/csr.csv

# 2. Проверьте адреса
cat build/csr.csv

# Должно быть примерно:
# csr_base,uart,0xf0001000,
# csr_base,timer0,0xf0000800,
# csr_base,my_slave,0x80000000,

# 3. Если адреса изменились, обновите DTS
cd ..
./update_dts_from_litex.sh
```

## Что описано в .dts

✅ **CPU**: VexRiscv RV32IM @ 48 MHz  
✅ **Memory Map**: Flash, SRAM с правильными адресами  
✅ **Peripherals**: UART, Timer с LiteX адресами  
✅ **SDIO Controller**: Полное описание с 15 регистрами  
✅ **Chosen nodes**: Console, Shell, RAM, Flash  
✅ **Properties**: Все необходимые свойства для Zephyr  

## Совместимость

- ✅ Zephyr 3.x+
- ✅ LiteX (любая версия с VexRiscv)
- ✅ Sipeed Tang Primer 20K FPGA
- ✅ VexRiscv standard variant
- ✅ Кастомные Wishbone периферийные устройства

## Примеры использования в коде

### Доступ через Device Tree API

```c
#include <zephyr/devicetree.h>

#define SDIO_NODE DT_NODELABEL(sdio0)
#define SDIO_BASE DT_REG_ADDR(SDIO_NODE)
#define SDIO_SIZE DT_REG_SIZE(SDIO_NODE)
#define SDIO_MAX_FREQ DT_PROP(SDIO_NODE, max_frequency)

printk("SDIO: 0x%08x (%d bytes)\n", SDIO_BASE, SDIO_SIZE);
printk("Max freq: %d Hz\n", SDIO_MAX_FREQ);
```

### Прямой доступ (совместимо с HAL)

```c
#define SDIO_BASE 0x80000000
#include "sdio_hal.h"

sdio_init(48000000, 100000);
```

## Следующие шаги

1. ✅ **DTS создан**
2. ⏭️ Собрать LiteX: `python3 sipeed_tang_primer_20k.py --build`
3. ⏭️ Обновить адреса: `./update_dts_from_litex.sh`
4. ⏭️ Собрать Zephyr: `west build -b litex_vexriscv_sdio`
5. ⏭️ Прошить FPGA: `python3 sipeed_tang_primer_20k.py --load`
6. ⏭️ Загрузить firmware: `litex_term --kernel zephyr.bin`

## Документация

Читайте в порядке:

1. **HOWTO_USE_DTS.md** ⭐ Как использовать DTS
2. **app/boards/.../README.md** - Board документация
3. **QUICKSTART_LITEX_ZEPHYR.md** - Шпаргалка
4. **ZEPHYR_LITEX_INTEGRATION.md** - Полное руководство

## Важные замечания

### Адреса UART/Timer могут измениться!

Если вы добавите/удалите периферию в LiteX:
```python
# sipeed_tang_primer_20k.py
self.add_spi_flash()  # ← Добавили SPI
```

Адреса CSR регистров могут сдвинуться! Всегда проверяйте `csr.csv` после сборки.

### SDIO адрес фиксирован

Адрес `0x80000000` задан явно в Python скрипте:
```python
region=SoCRegion(origin=0x8000_0000, ...)
```

Он не изменится, пока вы не измените его вручную.

## Файлы для справки

| Файл | Назначение |
|------|-----------|
| `litex_vexriscv_sdio.dts` | Device Tree - описание hardware |
| `*_defconfig` | Default Kconfig - конфигурация Zephyr |
| `board.cmake` | Build система - flash/load команды |
| `Kconfig.board` | Kconfig entry - интеграция в menuconfig |
| `board.yml` | Метаданные - arch, vendor, RAM/Flash |
| `README.md` | Документация board |

---

**Создано автоматически для проекта sdio_zephyr**  
**LiteX VexRiscv + SDIO Controller + Zephyr RTOS**
