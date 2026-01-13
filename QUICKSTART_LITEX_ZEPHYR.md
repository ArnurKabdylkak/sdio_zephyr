# LiteX + Zephyr: Шпаргалка по адаптации

## TL;DR - Главное про адреса

**Проблема:** Каждый LiteX SoC генерирует уникальные адреса CSR регистров.

**Решение:** LiteX автоматически генерирует `csr.json` → конвертируем в Zephyr Device Tree.

---

## Быстрый процесс адаптации (5 минут)

### 1. Собрать LiteX и получить адреса

```bash
cd litex/
python3 sipeed_tang_primer_20k.py --build --csr-json=build/csr.json
```

**Результат:** `build/csr.json` содержит все адреса вашего SoC.

### 2. Создать Device Tree overlay

**Вариант A: Автоматически**

```bash
litex_json2dts_zephyr build/csr.json > app/boards/litex_auto.dts
```

**Вариант B: Вручную (для кастомной периферии)**

Файл уже создан: `app/boards/litex_vexriscv.overlay`

```dts
/ {
    soc {
        sdio0: sdio@80000000 {  /* Адрес из sipeed_tang_primer_20k.py:247 */
            compatible = "litex,sdio";
            reg = <0x80000000 0x10000>;
        };
    };
};
```

### 3. Собрать Zephyr с overlay

```bash
cd app/
west build -b litex_vexriscv . -- \
    -DDTC_OVERLAY_FILE="boards/litex_vexriscv.overlay"
```

**Готово!** Zephyr теперь знает про ваш SDIO контроллер.

---

## Что делать при изменении LiteX конфигурации

### Сценарий 1: Добавили/удалили периферию

```bash
# 1. Пересобрать LiteX
cd litex/
python3 sipeed_tang_primer_20k.py --build --csr-json=build/csr.json

# 2. Проверить новые адреса
cat build/csr.csv | grep BASE

# 3. Обновить overlay (если адреса изменились)
# Вариант A: автогенерация
litex_json2dts_zephyr build/csr.json > /tmp/new.dts
# Скопируйте нужные части в app/boards/litex_vexriscv.overlay

# Вариант B: вручную через csr.h
grep "CSR_UART_BASE" build/software/include/generated/csr.h
# Обновите адрес в overlay

# 4. Пересобрать Zephyr
cd ../app/
west build -t clean
west build -b litex_vexriscv .
```

### Сценарий 2: Изменили только HDL (не добавляли периферию)

```bash
# Если адрес не менялся (origin=0x80000000), пересборка Zephyr НЕ нужна
cd litex/
python3 sipeed_tang_primer_20k.py --build

# Просто перепрошейте FPGA
python3 sipeed_tang_primer_20k.py --load
```

### Сценарий 3: Изменили адрес SDIO (origin=...)

```bash
# 1. Обновите overlay
sed -i 's/0x80000000/0xНОВЫЙ_АДРЕС/' app/boards/litex_vexriscv.overlay

# 2. Обновите HAL
sed -i 's/0x80000000/0xНОВЫЙ_АДРЕС/' litex/sdio_hal.h

# 3. Пересобрать Zephyr
cd app/
west build -t clean
west build -b litex_vexriscv .
```

---

## Проверка адресов после сборки LiteX

### Метод 1: CSV файл (самый простой)

```bash
cat litex/build/csr.csv

# Пример вывода:
# csr_base,uart,0xf0001000,
# csr_base,timer0,0xf0000800,
# csr_base,my_slave,0x80000000,  ← Ваш SDIO
```

### Метод 2: C header

```bash
grep "CSR_.*_BASE" litex/build/software/include/generated/csr.h

# Пример вывода:
# #define CSR_UART_BASE 0xf0001000L
# #define CSR_TIMER0_BASE 0xf0000800L
# #define CSR_MY_SLAVE_BASE 0x80000000L  ← Ваш SDIO
```

### Метод 3: JSON (для парсинга)

```bash
cat litex/build/csr.json | jq '.csr_bases'

# Пример вывода:
# {
#   "uart": 4026535936,        // 0xf0001000
#   "timer0": 4026533888,      // 0xf0000800
#   "my_slave": 2147483648     // 0x80000000
# }
```

---

## Использование сгенерированных адресов в коде

### Подход 1: Hardcoded (текущий, простой)

```c
// sdio_hal.h
#define SDIO_BASE 0x80000000  // Фиксированный адрес
```

**Плюсы:** Простота
**Минусы:** Нужно вручную менять при изменении LiteX

### Подход 2: Из LiteX headers (гибкий)

```c
// main.c
#include <generated/csr.h>  // Из litex/build/

#define SDIO_BASE CSR_MY_SLAVE_BASE  // Автоматически из LiteX
```

**Плюсы:** Автоматическое обновление
**Минусы:** Нужно копировать headers при каждой сборке

### Подход 3: Из Device Tree (Zephyr-way)

```c
// main.c
#include <zephyr/devicetree.h>

#define SDIO_NODE DT_NODELABEL(sdio0)
#define SDIO_BASE DT_REG_ADDR(SDIO_NODE)  // Из overlay
```

**Плюсы:** Zephyr-стиль, переносимость
**Минусы:** Требует создания bindings для "litex,sdio"

---

## Memory Map - как это работает

### LiteX генерирует адреса автоматически

```python
# sipeed_tang_primer_20k.py
self.bus.add_slave("my_slave", self.my_slave.bus,
    region=SoCRegion(
        origin = 0x8000_0000,  # ← Базовый адрес
        size = 0x10000,        # ← Размер региона
        cached = False
    ))
```

### Wishbone контроллер делит на регистры

```systemverilog
// WishboneController.sv
// Каждый регистр смещен на 0x1000 (из кода)
0x80000000 + 0x0000 = MAIN_CLOCK_FREQ
0x80000000 + 0x1000 = SD_CLOCK_FREQ
0x80000000 + 0x2000 = CMD_INDEX
...
```

### Zephyr получает через Device Tree

```dts
sdio0: sdio@80000000 {
    reg = <0x80000000 0x10000>;  // Тот же адрес!
};
```

---

## Автоматизация через скрипт

Создайте `sync_litex.sh`:

```bash
#!/bin/bash
set -e

echo "=== Rebuilding LiteX SoC ==="
cd litex/
python3 sipeed_tang_primer_20k.py --build --csr-json=build/csr.json

echo "=== Checking addresses ==="
SDIO_ADDR=$(cat build/csr.csv | grep my_slave | cut -d, -f3)
echo "SDIO Controller: $SDIO_ADDR"

if [ "$SDIO_ADDR" != "0x80000000" ]; then
    echo "WARNING: SDIO address changed!"
    echo "Update app/boards/litex_vexriscv.overlay"
fi

echo "=== Copying headers ==="
cd ..
mkdir -p app/include/generated
cp litex/build/software/include/generated/*.h app/include/generated/

echo "=== Rebuilding Zephyr ==="
cd app/
west build -t clean
west build -b litex_vexriscv .

echo "=== Done! ==="
```

Использование:

```bash
chmod +x sync_litex.sh
./sync_litex.sh
```

---

## Частые ошибки

### ❌ "region 'rom' overflowed"

**Причина:** ROM размер в LiteX не совпадает с Zephyr linker script.

**Решение:**
```bash
# Проверьте размер ROM в LiteX
grep "rom.*Region added" litex/build/*.log

# Обновите в overlay
&rom {
    reg = <0x00000000 0x20000>;  // Ваш размер из LiteX
};
```

### ❌ "SDIO address 0x80000000 not accessible"

**Причина:** CPU не имеет доступа к IO region.

**Решение:** В LiteX SoC должно быть:
```python
# VexRiscv добавляет IO region автоматически
INFO:SoC:CPU vexriscv adding IO Region 0 at 0x80000000
```

Проверьте лог сборки LiteX.

### ❌ Zephyr не видит overlay

**Причина:** Overlay не применяется.

**Решение:**
```bash
# Явно указывайте путь:
west build -- -DDTC_OVERLAY_FILE="${PWD}/boards/litex_vexriscv.overlay"

# Или через CMakeLists.txt:
list(APPEND DTC_OVERLAY_FILE "boards/litex_vexriscv.overlay")
```

---

## Итоговый чеклист

- [ ] Собрать LiteX → получить `csr.json`
- [ ] Проверить адреса в `csr.csv` или `csr.h`
- [ ] Создать/обновить Device Tree overlay
- [ ] Собрать Zephyr с `-DDTC_OVERLAY_FILE`
- [ ] Прошить FPGA bitstream
- [ ] Загрузить Zephyr binary
- [ ] Проверить логи через UART

---

## Документация

- **Полное руководство:** [ZEPHYR_LITEX_INTEGRATION.md](ZEPHYR_LITEX_INTEGRATION.md)
- **Инструкция по app:** [app/README_LITEX.md](app/README_LITEX.md)
- **HAL API:** [litex/README_HAL.md](litex/README_HAL.md)
