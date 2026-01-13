# Как использовать созданный .dts файл

## Что было создано

Полноценный **Zephyr board definition** для LiteX VexRiscv с SDIO:

```
app/boards/riscv/litex_vexriscv_sdio/
├── litex_vexriscv_sdio.dts            ← Полный Device Tree (это то, что вы просили!)
├── litex_vexriscv_sdio_defconfig      ← Default конфигурация
├── board.cmake                         ← Build скрипт
├── board.yml                           ← Метаинформация
├── Kconfig.board                       ← Kconfig опции
├── Kconfig.defconfig                   ← Default Kconfig значения
└── README.md                           ← Документация board
```

## Способ 1: Использовать как custom board (рекомендуется)

### Шаг 1: Сообщите Zephyr о board

```bash
# Установите BOARD_ROOT на директорию app/
export BOARD_ROOT=/home/arnurloq/sdio_zephyr/app

# Или добавьте в ~/.bashrc:
echo 'export BOARD_ROOT=/home/arnurloq/sdio_zephyr/app' >> ~/.bashrc
```

### Шаг 2: Проверьте, что Zephyr видит board

```bash
cd app/
west boards | grep litex

# Должно вывести:
# litex_vexriscv_sdio
```

### Шаг 3: Соберите приложение

```bash
cd app/
west build -b litex_vexriscv_sdio .
```

Готово! Zephyr использует ваш .dts файл автоматически.

---

## Способ 2: Копировать в Zephyr tree (для постоянного использования)

### Шаг 1: Скопируйте board в Zephyr

```bash
# Найдите Zephyr root
ZEPHYR_BASE=$(west topdir)/zephyr

# Скопируйте board
cp -r app/boards/riscv/litex_vexriscv_sdio \
      $ZEPHYR_BASE/boards/riscv/
```

### Шаг 2: Соберите без BOARD_ROOT

```bash
cd app/
west build -b litex_vexriscv_sdio .
```

---

## Способ 3: Использовать только .dts как overlay (упрощенный)

Если не хотите создавать полный board, используйте только .dts как overlay:

```bash
cd app/
west build -b litex_vexriscv . -- \
    -DDTC_OVERLAY_FILE="${PWD}/boards/riscv/litex_vexriscv_sdio/litex_vexriscv_sdio.dts"
```

**Примечание:** Для этого нужен базовый board `litex_vexriscv` в Zephyr.

---

## Обновление адресов из LiteX

### Автоматически (рекомендуется)

Используйте скрипт:

```bash
# 1. Соберите LiteX
cd litex/
python3 sipeed_tang_primer_20k.py --build --csr-csv=build/csr.csv

# 2. Обновите DTS автоматически
cd ..
./update_dts_from_litex.sh
```

### Вручную

```bash
# 1. Посмотрите адреса
cat litex/build/csr.csv

# Пример вывода:
# csr_base,uart,0xf0001000,
# csr_base,timer0,0xf0000800,
# csr_base,my_slave,0x80000000,

# 2. Откройте DTS и обновите адреса
vim app/boards/riscv/litex_vexriscv_sdio/litex_vexriscv_sdio.dts

# Найдите и замените:
uart0: serial@0xf0001000 {  ← Обновите этот адрес
    reg = <0xf0001000 0x100>;
    ...
}

timer0: timer@0xf0000800 {  ← Обновите этот адрес
    reg = <0xf0000800 0x40>;
    ...
}

sdio0: sdio@0x80000000 {    ← Обычно не меняется
    reg = <0x80000000 0x10000>;
    ...
}
```

---

## Проверка корректности .dts

### Шаг 1: Соберите Zephyr

```bash
cd app/
west build -b litex_vexriscv_sdio .
```

### Шаг 2: Проверьте скомпилированный DTS

```bash
# Посмотрите финальный Device Tree
cat build/zephyr/zephyr.dts

# Или только SDIO секцию
cat build/zephyr/zephyr.dts | grep -A 50 "sdio@"
```

Должно быть:

```dts
sdio0: sdio@80000000 {
    compatible = "litex,sdio";
    reg = <0x80000000 0x10000>;
    status = "okay";
    ...
};
```

### Шаг 3: Проверьте memory regions

```bash
riscv64-unknown-elf-objdump -h build/zephyr/zephyr.elf | grep -E "\.text|\.data|\.bss"
```

Должно быть в пределах SRAM (0x10000000 - 0x10002000).

---

## Использование в коде

### Вариант A: Через Device Tree API

```c
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>

#define SDIO_NODE DT_NODELABEL(sdio0)

#if DT_NODE_EXISTS(SDIO_NODE)
    #define SDIO_BASE DT_REG_ADDR(SDIO_NODE)
    #define SDIO_SIZE DT_REG_SIZE(SDIO_NODE)
#else
    #error "SDIO node not found in Device Tree!"
#endif

int main(void) {
    printk("SDIO Base: 0x%08x\n", SDIO_BASE);
    printk("SDIO Size: 0x%08x\n", SDIO_SIZE);
    return 0;
}
```

### Вариант B: Напрямую (проще)

```c
#include <zephyr/kernel.h>

// Адрес из DTS
#define SDIO_BASE 0x80000000

#include "../../litex/sdio_hal.h"

int main(void) {
    sdio_init(48000000, 100000);

    sdio_response_t resp;
    sdio_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, &resp);

    return 0;
}
```

---

## Пример полной сборки

```bash
# 1. Собрать LiteX SoC
cd litex/
python3 sipeed_tang_primer_20k.py --build --csr-csv=build/csr.csv

# 2. Прошить FPGA
python3 sipeed_tang_primer_20k.py --load

# 3. Обновить DTS (если адреса изменились)
cd ..
./update_dts_from_litex.sh

# 4. Установить BOARD_ROOT
export BOARD_ROOT=$(pwd)/app

# 5. Собрать Zephyr приложение
cd app/
west build -b litex_vexriscv_sdio .

# 6. Загрузить на FPGA через UART
litex_term --kernel build/zephyr/zephyr.bin /dev/ttyUSB1
```

---

## Типичные ошибки

### ❌ "Board litex_vexriscv_sdio not found"

**Причина:** Zephyr не видит board.

**Решение:**
```bash
export BOARD_ROOT=/home/arnurloq/sdio_zephyr/app
west boards | grep litex  # Должен показать board
```

### ❌ "region 'rom' overflowed by X bytes"

**Причина:** Приложение слишком большое для ROM (128 KB).

**Решение:**
- Уменьшите размер приложения
- Или увеличьте ROM в LiteX и обновите DTS:
  ```dts
  flash0: flash@0 {
      reg = <0x00000000 0x40000>;  // 256 KB вместо 128 KB
  };
  ```

### ❌ "UART not working"

**Причина:** Неправильный адрес UART в DTS.

**Решение:**
```bash
# Проверьте адрес в LiteX
grep CSR_UART_BASE litex/build/software/include/generated/csr.h

# Обновите в DTS
vim app/boards/riscv/litex_vexriscv_sdio/litex_vexriscv_sdio.dts
```

### ❌ "Address 0x80000000 not accessible"

**Причина:** SDIO контроллер не добавлен в Wishbone bus или FPGA не прошита.

**Решение:**
1. Проверьте sipeed_tang_primer_20k.py:247 - должно быть `self.bus.add_slave("my_slave", ...)`
2. Пересоберите и прошейте FPGA
3. Проверьте логи LiteX при сборке

---

## Структура .dts файла

Ваш DTS файл содержит:

```dts
/dts-v1/;

/ {
    compatible = "litex,vexriscv";

    chosen { ... };          // Консоль, RAM, Flash

    cpus {                   // CPU описание
        cpu@0 { ... };
    };

    soc {                    // Системная шина
        flash0 { ... };      // ROM
        sram0 { ... };       // RAM
        uart0 { ... };       // UART
        timer0 { ... };      // Timer
        sdio0 { ... };       // ← Ваш SDIO контроллер!
    };
};
```

**SDIO секция** содержит:
- `compatible = "litex,sdio"` - идентификатор драйвера
- `reg = <0x80000000 0x10000>` - адрес и размер
- `litex,reg-*` - offset'ы регистров
- `max-frequency`, `bus-width`, etc - параметры

---

## Следующие шаги

1. ✅ **DTS создан** - готово!
2. ⏭️ Собрать LiteX и получить csr.csv
3. ⏭️ Обновить адреса через `./update_dts_from_litex.sh`
4. ⏭️ Собрать Zephyr с `west build -b litex_vexriscv_sdio`
5. ⏭️ Протестировать на железе

---

## Документация

- **Board README:** `app/boards/riscv/litex_vexriscv_sdio/README.md`
- **Быстрый старт:** `QUICKSTART_LITEX_ZEPHYR.md`
- **Полное руководство:** `ZEPHYR_LITEX_INTEGRATION.md`
- **Скрипт обновления:** `update_dts_from_litex.sh`
