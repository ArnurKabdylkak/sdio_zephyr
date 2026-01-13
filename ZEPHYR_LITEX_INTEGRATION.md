# Интеграция LiteX SoC с Zephyr: Полное руководство

## Проблема
Каждый LiteX SoC имеет уникальные адреса регистров (CSR), которые генерируются автоматически в зависимости от конфигурации. Zephyr нужно "научить" работать с вашей конкретной конфигурацией.

## Решение: 3 подхода

### Подход 1: Custom Board Definition (Рекомендуется для production)

Создаем полноценный board в Zephyr с автогенерацией из LiteX.

#### Шаг 1.1: Генерация CSR файлов из LiteX

```bash
cd litex/
python3 sipeed_tang_primer_20k.py --build \
    --cpu-type=vexriscv \
    --cpu-variant=standard \
    --csr-json=build/csr.json \
    --csr-csv=build/csr.csv
```

Это создаст:
- `build/csr.json` - полное описание регистров
- `build/csr.csv` - CSV версия
- `build/software/include/generated/csr.h` - C header с адресами

#### Шаг 1.2: Генерация Device Tree для Zephyr

```bash
# Конвертируем LiteX CSR в Zephyr Device Tree
litex_json2dts_zephyr build/csr.json > litex_tang_primer_20k.dts
```

Это сгенерирует DTS файл с правильными адресами всех периферийных устройств.

#### Шаг 1.3: Создание board definition в Zephyr

Структура (в вашем Zephyr tree или out-of-tree):

```
boards/riscv/litex_tang_primer_20k_sdio/
├── board.cmake
├── board.yml
├── CMakeLists.txt
├── Kconfig.board
├── Kconfig.defconfig
├── litex_tang_primer_20k_sdio.dts      # Из litex_json2dts_zephyr + ваши изменения
├── litex_tang_primer_20k_sdio.yaml
└── litex_tang_primer_20k_sdio_defconfig
```

**litex_tang_primer_20k_sdio.dts:**
```dts
/dts-v1/;

/ {
    #address-cells = <1>;
    #size-cells = <1>;
    compatible = "litex,vexriscv";
    model = "LiteX VexRiscv SoC on Tang Primer 20K with SDIO";

    chosen {
        zephyr,console = &uart0;
        zephyr,shell-uart = &uart0;
        zephyr,sram = &ram0;
    };

    cpus {
        #address-cells = <1>;
        #size-cells = <0>;
        cpu@0 {
            compatible = "riscv";
            device_type = "cpu";
            reg = <0>;
            riscv,isa = "rv32im";
            mmu-type = "riscv,none";
            clock-frequency = <48000000>;
        };
    };

    soc {
        #address-cells = <1>;
        #size-cells = <1>;
        compatible = "litex,vexriscv";
        ranges;

        ram0: memory@10000000 {
            device_type = "memory";
            compatible = "mmio-sram";
            reg = <0x10000000 0x2000>;  // Из LiteX build
        };

        uart0: serial@f0001000 {  // Адрес из csr.json
            compatible = "litex,uart0";
            reg = <0xf0001000 0x100>;
            status = "okay";
        };

        timer0: timer@f0000800 {  // Адрес из csr.json
            compatible = "litex,timer0";
            reg = <0xf0000800 0x40>;
            status = "okay";
        };

        /* ВАШ КАСТОМНЫЙ SDIO КОНТРОЛЛЕР */
        sdio0: sdio@80000000 {
            compatible = "litex,sdio";
            reg = <0x80000000 0x10000>;
            reg-io-width = <4>;
            status = "okay";

            /* Маппинг регистров (ваши offset'ы) */
            litex,main-clk-freq-offset = <0x0000>;
            litex,sd-clk-freq-offset = <0x1000>;
            litex,cmd-index-offset = <0x2000>;
            litex,cmd-argument-offset = <0x3000>;
            litex,data-buffer-offset = <0x4000>;
            litex,send-cmd-op-offset = <0x5000>;
            litex,send-cmd-read-data-op-offset = <0x6000>;
            litex,send-cmd-send-data-op-offset = <0x7000>;
            litex,read-data-op-offset = <0x8000>;
            litex,send-data-op-offset = <0x9000>;
            litex,cmd-busy-offset = <0xA000>;
            litex,data-busy-offset = <0xB000>;
            litex,cmd-status-offset = <0xC000>;
            litex,data-status-offset = <0xD000>;
            litex,data-length-offset = <0xE000>;
        };
    };
};

&uart0 {
    status = "okay";
    current-speed = <115200>;
};
```

**Kconfig.board:**
```kconfig
config BOARD_LITEX_TANG_PRIMER_20K_SDIO
	bool "LiteX VexRiscv Tang Primer 20K with SDIO"
	depends on SOC_RISCV32_LITEX_VEXRISCV
```

**litex_tang_primer_20k_sdio_defconfig:**
```kconfig
CONFIG_BOARD_LITEX_TANG_PRIMER_20K_SDIO=y
CONFIG_SOC_RISCV32_LITEX_VEXRISCV=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_SERIAL=y
CONFIG_RISCV_MACHINE_TIMER=y
```

#### Шаг 1.4: Сборка приложения

```bash
cd app/
west build -b litex_tang_primer_20k_sdio .
```

---

### Подход 2: Overlay файлы (Рекомендуется для прототипирования)

Быстрый способ без создания полного board definition.

#### Шаг 2.1: Используем существующий LiteX board

Zephyr уже имеет базовый LiteX VexRiscv board. Проверим:

```bash
west boards | grep litex
```

Если есть, используем его как основу.

#### Шаг 2.2: Создаем overlay в вашем приложении

```
app/
├── boards/
│   └── litex_vexriscv.overlay
├── CMakeLists.txt
├── prj.conf
└── src/
    └── main.c
```

**boards/litex_vexriscv.overlay:**
```dts
/ {
    chosen {
        zephyr,sram = &ram0;
    };

    soc {
        /* Добавляем ваш SDIO контроллер */
        sdio0: sdio@80000000 {
            compatible = "litex,sdio";
            reg = <0x80000000 0x10000>;
            status = "okay";
        };
    };
};

/* Переопределяем размер RAM если нужно (из csr.json) */
&ram0 {
    reg = <0x10000000 0x2000>;  /* Ваш размер из LiteX */
};
```

#### Шаг 2.3: Сборка с overlay

```bash
west build -b litex_vexriscv . -- \
    -DDTC_OVERLAY_FILE="boards/litex_vexriscv.overlay"
```

---

### Подход 3: Прямое использование LiteX headers (Самый простой)

Не используем Device Tree вообще, только C headers из LiteX.

#### Шаг 3.1: Копируем generated headers

```bash
cp litex/build/software/include/generated/csr.h app/include/
cp litex/build/software/include/generated/soc.h app/include/
cp litex/build/software/include/generated/mem.h app/include/
```

#### Шаг 3.2: Используем в коде

```c
// app/src/main.c
#include <zephyr/kernel.h>

// Используем сгенерированные LiteX headers
#include <generated/csr.h>
#include <generated/soc.h>

// Ваш HAL с адресами из LiteX
#define SDIO_BASE CSR_MY_SLAVE_BASE  // Автоматически из csr.h

#include "../../litex/sdio_hal.h"

int main(void) {
    printk("SDIO Base Address: 0x%08x\n", SDIO_BASE);

    // HAL работает напрямую
    sdio_init(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC, 100000);

    return 0;
}
```

**CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(litex_sdio_app)

# Добавляем LiteX generated headers
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../litex/build/software/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../litex
)

target_sources(app PRIVATE
    src/main.c
    ../litex/sdio_hal.c
)
```

---

## Сравнение подходов

| Подход | Сложность | Переносимость | Использование DT | Когда использовать |
|--------|-----------|---------------|------------------|-------------------|
| Custom Board | Высокая | Высокая | Да | Production, несколько проектов |
| Overlay | Средняя | Средняя | Да | Прототипирование, один проект |
| Direct Headers | Низкая | Низкая | Нет | Быстрый прототип, debugging |

---

## Автоматизация процесса

### Скрипт для синхронизации LiteX → Zephyr

```bash
#!/bin/bash
# sync_litex_to_zephyr.sh

set -e

LITEX_DIR="litex"
APP_DIR="app"
BOARD_NAME="litex_tang_primer_20k_sdio"

echo "=== Rebuilding LiteX SoC ==="
cd $LITEX_DIR
python3 sipeed_tang_primer_20k.py --build \
    --csr-json=build/csr.json

echo "=== Generating Zephyr DTS ==="
litex_json2dts_zephyr build/csr.json > build/litex_generated.dts

echo "=== Copying headers to app ==="
cd ..
mkdir -p $APP_DIR/include/generated
cp $LITEX_DIR/build/software/include/generated/*.h $APP_DIR/include/generated/

echo "=== Done! ==="
echo "CSR base addresses changed? Update your overlay or DTS file:"
echo "  - SDIO: 0x80000000 (hardcoded in your SoC)"
echo "  - UART: $(grep CSR_UART_BASE $LITEX_DIR/build/software/include/generated/csr.h)"
```

---

## Проверка адресов

После каждой пересборки LiteX, проверяйте адреса:

```bash
# Смотрим адреса всех CSR регистров
cat litex/build/csr.csv

# Или в csr.h
grep "CSR_.*_BASE" litex/build/software/include/generated/csr.h
```

Пример вывода:
```
CSR_UART_BASE 0xf0001000
CSR_TIMER0_BASE 0xf0000800
CSR_MY_SLAVE_BASE 0x80000000  // Ваш SDIO
```

---

## Рекомендация для вашего проекта

Для **sdio_zephyr** проекта рекомендую **Подход 2 (Overlay)**:

1. Адрес SDIO жестко закодирован (`0x80000000`) в вашем LiteX SoC
2. Нужен быстрый прототип
3. Не требуется переносимость на другие платформы пока

Далее можно мигрировать на Подход 1 когда понадобится.
