# Резюме: Адаптация Zephyr для LiteX SoC

## Что было сделано

Создана полная интеграция LiteX SDIO контроллера с Zephyr RTOS, с автоматической адаптацией к уникальным адресам каждого SoC.

## Созданные файлы

### 📚 Документация

1. **QUICKSTART_LITEX_ZEPHYR.md** - Шпаргалка (5 минут)
   - Быстрый процесс адаптации
   - Проверка адресов после сборки LiteX
   - Что делать при изменении конфигурации
   - Скрипт автоматизации

2. **ZEPHYR_LITEX_INTEGRATION.md** - Полное руководство
   - 3 подхода к интеграции
   - Custom Board Definition
   - Overlay файлы
   - Direct Headers
   - Сравнение подходов

3. **app/README_LITEX.md** - Инструкция для app/
   - Быстрый старт
   - Структура проекта
   - Device Tree overlay
   - Debugging tips
   - FAQ

### 🛠️ Код и конфигурация

4. **app/boards/litex_vexriscv.overlay** - Device Tree overlay
   - SDIO controller @ 0x80000000
   - Все регистры с offset'ами
   - Готов к использованию

5. **app/src/main_litex.c** - Пример приложения для LiteX
   - Использует HAL из litex/
   - Полная последовательность инициализации SDIO
   - CMD0, CMD5, CMD3, CMD7
   - Чтение CCCR регистров
   - Включение Function 1
   - Zephyr logging

6. **app/CMakeLists.txt** - Обновлен
   - Подключает ../litex/sdio_hal.c
   - Include paths для HAL

7. **app/prj.conf** - Обновлен
   - Комментарии для LiteX конфигурации

## Главное: как адаптировать Zephyr под ваш SoC

### Простое объяснение

**Проблема:** У каждого LiteX SoC разные адреса регистров.

**Решение в 3 шага:**

```bash
# 1. Собрать LiteX → получить адреса
cd litex/
python3 sipeed_tang_primer_20k.py --build --csr-json=build/csr.json

# 2. Проверить адреса
cat build/csr.csv | grep my_slave
# Вывод: csr_base,my_slave,0x80000000,

# 3. Использовать в Zephyr
cd app/
west build -b litex_vexriscv . -- \
    -DDTC_OVERLAY_FILE="boards/litex_vexriscv.overlay"
```

### Детально

#### Вариант A: Overlay файл (рекомендуется для прототипа)

Уже создан: `app/boards/litex_vexriscv.overlay`

Содержит:
- Базовый адрес SDIO: `0x80000000`
- Размер региона: `0x10000`
- Offset'ы всех регистров

**Когда обновлять:**
- Изменили `origin=...` в sipeed_tang_primer_20k.py
- Добавили/удалили периферию (UART, Timer могут сдвинуться)

**Не нужно обновлять:**
- Изменили только HDL (WishboneController.sv)
- Изменили внутреннюю логику контроллера

#### Вариант B: Custom Board (для production)

Создайте полноценный board в Zephyr:

```bash
# Автоматическая генерация DTS
litex_json2dts_zephyr litex/build/csr.json > my_board.dts
```

#### Вариант C: Direct Headers (самый простой)

Используйте сгенерированные LiteX headers напрямую:

```c
#include <generated/csr.h>
#define SDIO_BASE CSR_MY_SLAVE_BASE  // Автоматически!
```

## Ваш текущий адрес SDIO

```python
# litex/sipeed_tang_primer_20k.py:247
self.bus.add_slave("my_slave", self.my_slave.bus,
    region=SoCRegion(
        origin = 0x8000_0000,  # ← Адрес SDIO
        size = 0x10000,
        cached = False
    ))
```

Этот адрес **жестко закодирован** в вашем Python скрипте, поэтому не изменится при пересборке.

## Тестирование

### На LiteX VexRiscv

```bash
# 1. Собрать LiteX SoC
cd litex/
python3 sipeed_tang_primer_20k.py --build

# 2. Прошить FPGA
python3 sipeed_tang_primer_20k.py --load

# 3. Собрать Zephyr приложение
cd ../app/

# Используйте main_litex.c вместо main.c:
mv src/main.c src/main_rp2350.c
mv src/main_litex.c src/main.c

west build -b litex_vexriscv . -- \
    -DDTC_OVERLAY_FILE="boards/litex_vexriscv.overlay"

# 4. Загрузить через litex_term или UART
west flash
# или
litex_term --kernel build/zephyr/zephyr.bin /dev/ttyUSB1
```

### На RP2350 (текущая версия)

```bash
# Используйте текущий main.c (bit-banging)
cd app/
west build -b rpi_pico2_rp2350a_hazard3 .
west flash
```

## Следующие шаги

1. ✅ **Понять процесс адаптации** - готово!
2. ⏭️ Собрать LiteX и проверить адреса
3. ⏭️ Протестировать app/src/main_litex.c на железе
4. ⏭️ Реализовать CMD53 для multi-byte transfer
5. ⏭️ Добавить backplane access
6. ⏭️ Загрузить firmware в CYW55500

## Быстрая справка

### Проверить адреса LiteX

```bash
# CSV
cat litex/build/csr.csv

# C header
grep CSR_MY_SLAVE_BASE litex/build/software/include/generated/csr.h

# JSON
cat litex/build/csr.json | jq '.csr_bases.my_slave'
```

### Обновить overlay при изменении адресов

```bash
# Сгенерировать новый DTS
litex_json2dts_zephyr litex/build/csr.json > /tmp/new.dts

# Сравнить с текущим overlay
diff app/boards/litex_vexriscv.overlay /tmp/new.dts

# Обновить вручную или скопировать нужные части
```

### Отладка в Zephyr

```bash
# Посмотреть финальный Device Tree
cat app/build/zephyr/zephyr.dts | grep sdio -A 20

# Посмотреть символы
riscv64-unknown-elf-nm app/build/zephyr/zephyr.elf | grep sdio

# Посмотреть память regions
riscv64-unknown-elf-objdump -h app/build/zephyr/zephyr.elf
```

## Структура проекта

```
sdio_zephyr/
├── QUICKSTART_LITEX_ZEPHYR.md      # Шпаргалка (начните здесь!)
├── ZEPHYR_LITEX_INTEGRATION.md     # Полное руководство
├── SUMMARY.md                       # Этот файл
│
├── litex/
│   ├── sipeed_tang_primer_20k.py   # LiteX SoC definition
│   ├── sdio_hal.h                  # SDIO HAL API
│   ├── sdio_hal.c                  # SDIO HAL implementation
│   ├── README_HAL.md               # HAL documentation
│   └── build/
│       ├── csr.json                # Сгенерированные адреса
│       └── software/include/generated/
│           └── csr.h               # C header с адресами
│
└── app/
    ├── README_LITEX.md             # Инструкция для app/
    ├── CMakeLists.txt              # Обновлен для HAL
    ├── prj.conf                    # Zephyr config
    ├── boards/
    │   ├── litex_vexriscv.overlay  # Device Tree overlay для LiteX
    │   └── rpi_pico2_*.overlay     # Device Tree для RP2350
    └── src/
        ├── main_litex.c            # Zephyr + LiteX HAL (новый!)
        └── main.c                  # RP2350 bit-banging (текущий)
```

## Ключевые концепции

### LiteX генерирует адреса автоматически

```python
# В Python скрипте:
self.bus.add_slave("my_slave", ..., 
    region=SoCRegion(origin=0x80000000, ...))

# ↓ LiteX build генерирует ↓

# csr.csv:
csr_base,my_slave,0x80000000,

# csr.h:
#define CSR_MY_SLAVE_BASE 0x80000000L
```

### Zephyr получает через Device Tree

```dts
// litex_vexriscv.overlay
sdio0: sdio@80000000 {
    reg = <0x80000000 0x10000>;
};
```

### HAL использует напрямую

```c
// sdio_hal.h
#define SDIO_BASE 0x80000000

// или из LiteX:
#include <generated/csr.h>
#define SDIO_BASE CSR_MY_SLAVE_BASE
```

### Всё работает вместе!

```
LiteX SoC (Python) → генерирует csr.json
                   ↓
         litex_json2dts_zephyr
                   ↓
              Device Tree (DTS)
                   ↓
         Zephyr (C code) + HAL
```

## Контакты и ресурсы

- [LiteX](https://github.com/enjoy-digital/litex)
- [Zephyr Project](https://www.zephyrproject.org/)
- [VexRiscv](https://github.com/SpinalHDL/VexRiscv)
