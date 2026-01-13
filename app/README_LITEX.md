# LiteX SDIO - Интеграция с Zephyr

## Быстрый старт

### 1. Соберите LiteX SoC и получите адреса регистров

```bash
cd ../litex/
python3 sipeed_tang_primer_20k.py --build --csr-json=build/csr.json
```

Это сгенерирует:
- `build/csr.json` - JSON описание всех регистров
- `build/software/include/generated/csr.h` - C header с адресами

### 2. Проверьте адреса (опционально)

```bash
# Посмотрите адрес вашего SDIO контроллера
grep "CSR_MY_SLAVE_BASE" build/software/include/generated/csr.h

# Должно быть: #define CSR_MY_SLAVE_BASE 0x80000000
```

### 3. Выберите целевую платформу

**Вариант A: LiteX VexRiscv (рекомендуется)**

```bash
# Убедитесь, что у вас есть LiteX board в Zephyr:
west boards | grep litex

# Если нет, используйте overlay файл (уже создан):
# app/boards/litex_vexriscv.overlay
```

**Вариант B: RP2350 (для тестирования на другой платформе)**

Используйте текущий `main.c` с bit-banging через GPIO.

### 4. Соберите приложение для LiteX

```bash
cd app/

# С overlay файлом (быстрый способ):
west build -b litex_vexriscv . -- \
    -DDTC_OVERLAY_FILE="boards/litex_vexriscv.overlay" \
    -DCONFIG_FILE="prj_litex.conf"

# Или создайте отдельный main файл:
# Переименуйте src/main.c в src/main_rp2350.c
# Переименуйте src/main_litex.c в src/main.c
```

### 5. Прошейте FPGA и загрузите приложение

```bash
# Прошивка FPGA (из litex/)
cd ../litex/
python3 sipeed_tang_primer_20k.py --load

# Загрузка Zephyr приложения (зависит от вашего метода загрузки)
# Например, через UART bootloader или litex_term
cd ../app/
west flash

# Или через litex_term:
litex_term --kernel build/zephyr/zephyr.bin /dev/ttyUSB1
```

---

## Структура проекта

```
app/
├── boards/
│   └── litex_vexriscv.overlay    # Device Tree overlay для SDIO
├── CMakeLists.txt                 # Подключает HAL из ../litex/
├── prj.conf                       # Конфигурация Zephyr
├── README_LITEX.md               # Этот файл
└── src/
    ├── main.c                     # RP2350 bit-banging (текущий)
    └── main_litex.c               # LiteX HAL integration (новый)
```

---

## Device Tree Overlay

Файл `boards/litex_vexriscv.overlay` добавляет SDIO контроллер в Device Tree:

```dts
/ {
    soc {
        sdio0: sdio@80000000 {
            compatible = "litex,sdio";
            reg = <0x80000000 0x10000>;
            status = "okay";
            /* ... регистры ... */
        };
    };
};
```

**Важно:** Адрес `0x80000000` жестко задан в `sipeed_tang_primer_20k.py:247`.

Если вы измените конфигурацию LiteX (добавите/уберете периферию), адреса других устройств (UART, Timer) могут измениться! Обновите overlay:

```bash
# Сгенерируйте новый DTS из csr.json
litex_json2dts_zephyr ../litex/build/csr.json > generated.dts

# Скопируйте нужные части в overlay
```

---

## Использование HAL в Zephyr

### Прямой доступ (текущий подход)

```c
#include <zephyr/kernel.h>

#define SDIO_BASE 0x80000000
#include "sdio_hal.h"

int main(void) {
    // Инициализация HAL
    sdio_init(48000000, 100000);

    // Отправка команды
    sdio_response_t resp;
    sdio_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, &resp);

    return 0;
}
```

### Через Zephyr Device API (будущее)

Можно создать полноценный Zephyr driver:

```
drivers/sdio/
└── sdio_litex.c    # Zephyr SDIO driver обертка над HAL
```

Но для прототипа достаточно прямого доступа.

---

## Отладка

### Проблема: "Device not found"

Zephyr не находит SDIO устройство в Device Tree.

**Решение:**
1. Проверьте, что overlay применяется:
   ```bash
   west build -t menuconfig
   # Или посмотрите build/zephyr/zephyr.dts
   ```

2. Убедитесь, что в CMakeLists.txt указан путь к overlay

### Проблема: "Address access fault"

Zephyr не может обратиться по адресу 0x80000000.

**Решение:**
1. Проверьте, что LiteX SoC прошит на FPGA
2. Проверьте Memory map в LiteX:
   ```python
   # sipeed_tang_primer_20k.py:247
   region=SoCRegion(origin=0x8000_0000, ...)
   ```
3. Убедитесь, что CPU имеет доступ к IO region (строка 246):
   ```python
   self.bus.add_slave("my_slave", ...)
   ```

### Проблема: "Command timeout"

SDIO команды не получают ответ.

**Решение:**
1. Проверьте подключение WiFi модуля к FPGA пинам
2. Проверьте, что модуль получает питание
3. Добавьте pull-up резисторы на CMD и DAT линии
4. Проверьте частоту SDIO clock (100kHz для инициализации)

---

## Сравнение с RP2350 версией

| Аспект | RP2350 (main.c) | LiteX (main_litex.c) |
|--------|-----------------|---------------------|
| Метод | Bit-banging GPIO | Hardware SDIO controller |
| Скорость | ~100kHz max | До 50MHz |
| CPU нагрузка | Высокая | Низкая |
| Надежность | Зависит от timing | Аппаратный CRC |
| Сложность кода | Высокая | Низкая (HAL) |
| Debugging | Сложно | Проще |

---

## Следующие шаги

1. **Протестировать на LiteX:** Соберите и запустите `main_litex.c`
2. **CMD53 transfer:** Реализуйте multi-byte data transfer
3. **Backplane access:** Прочитайте Chip ID из CYW55500
4. **Firmware download:** Загрузите cyfmac55500-sdio.trxse
5. **WiFi operations:** Сканирование, подключение к AP

---

## Полезные команды

```bash
# Пересборка LiteX после изменений в HDL
cd litex/
python3 sipeed_tang_primer_20k.py --build

# Просмотр сгенерированных адресов
cat build/csr.csv

# Генерация DTS для Zephyr
litex_json2dts_zephyr build/csr.json > litex.dts

# Чистая пересборка Zephyr
cd app/
west build -t clean
west build -b litex_vexriscv .

# Просмотр финального Device Tree
cat build/zephyr/zephyr.dts
```

---

## FAQ

**Q: Нужно ли пересобирать Zephyr при изменении LiteX?**

A: Зависит от изменений:
- Изменили HDL (WishboneController.sv) → НЕТ (адреса не изменились)
- Добавили/удалили периферию в LiteX → ДА (адреса могут сдвинуться)
- Изменили `origin=0x80000000` → ДА (нужно обновить overlay)

**Q: Можно ли использовать один код для RP2350 и LiteX?**

A: Да, через условную компиляцию:

```c
#ifdef CONFIG_BOARD_LITEX_VEXRISCV
    #include "sdio_hal.h"
    sdio_init(...);
#else
    // RP2350 bit-banging code
#endif
```

**Q: Как узнать, какой board использовать в west build?**

A: Проверьте доступные LiteX boards:

```bash
west boards | grep litex
west boards | grep vexriscv
```

Если нет подходящего, используйте overlay с любым RISC-V board.

---

## Ресурсы

- [LiteX Documentation](https://github.com/enjoy-digital/litex)
- [Zephyr Device Tree Guide](https://docs.zephyrproject.org/latest/build/dts/index.html)
- [VexRiscv CPU](https://github.com/SpinalHDL/VexRiscv)
- [../litex/README_HAL.md](../litex/README_HAL.md) - SDIO HAL API
- [ZEPHYR_LITEX_INTEGRATION.md](../ZEPHYR_LITEX_INTEGRATION.md) - Подробное руководство
