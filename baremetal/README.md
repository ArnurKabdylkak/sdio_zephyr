# CYW55500 WiFi Bare-metal Driver для LiteX RISC-V

Этот проект представляет собой bare-metal драйвер для WiFi чипа Infineon CYW55500, который используется в модулях Quectel FCS96xN-LP. Драйвер разработан для работы на RISC-V процессоре в составе LiteX SoC и взаимодействует с чипом через SDIO интерфейс.


## Структура проекта

```
baremetal/
├── main.c              # Точка входа и пример использования
├── cyw55500_sdio.c     # Драйвер WiFi чипа CYW55500
├── cyw55500_sdio.h     # API драйвера
├── cyw55500_regs.h     # Все регистры чипа CYW55500
├── sdio_litex.c        # HAL — работа с SDIO контроллером LiteX
├── sdio_litex.h        # Определения регистров SDIO контроллера
├── baremetal.h         # Общие определения (типы, макросы)
├── libc.c              # Минимальная libc (memset, memcpy, strlen)
├── startup.S           # Код запуска процессора
├── linker.ld           # Скрипт линковки (память, секции)
├── Makefile            # Система сборки
└── README.md           # Этот файл
```

## Как это всё работает вместе

### Уровень 1: Hardware (FPGA)

В FPGA реализован SDIO контроллер на Verilog. Он подключен к шине Wishbone и имеет набор регистров по адресу `0x80000000`. Контроллер умеет отправлять SDIO команды (CMD0, CMD5, CMD52, CMD53 и т.д.) и передавать данные на WiFi чип.

Регистры контроллера:
- `0x80000000` — частота основного клока
- `0x80001000` — частота SDIO клока
- `0x80002000` — индекс команды
- `0x80003000` — аргумент команды
- `0x80004000` — буфер данных
- `0x80005000` — триггер отправки команды
- ... и так далее

### Уровень 2: HAL (sdio_litex.c)

HAL (Hardware Abstraction Layer) — это прослойка между железом и драйвером. Она скрывает детали работы с регистрами SDIO контроллера.

**Что делает HAL:**
- Инициализирует SDIO карту (CMD0 → CMD5 → CMD3 → CMD7)
- Отправляет CMD52 — чтение/запись одного байта
- Отправляет CMD53 — чтение/запись блока данных
- Управляет 4-bit режимом и скоростью

**Пример работы CMD52:**
```c
// Хотим прочитать байт из регистра 0x1000E на Function 1
int litex_sdio_cmd52_read(uint8_t func, uint32_t addr, uint8_t *val)
{
    // Формируем аргумент CMD52:
    // [31] = 0 (чтение), [30:28] = func, [25:9] = addr
    uint32_t arg = ((func & 0x7) << 28) | ((addr & 0x1FFFF) << 9);

    // Записываем в регистры контроллера
    REG32(SDIO_REG_CMD_INDEX) = 52;
    REG32(SDIO_REG_CMD_ARGUMENT) = arg;

    // Запускаем команду
    while (REG32(SDIO_REG_SEND_CMD) != 0);

    // Ждём завершения
    while (REG32(SDIO_REG_CMD_BUSY));

    // Читаем ответ
    *val = REG32(SDIO_REG_CMD_ARGUMENT) & 0xFF;
    return 0;
}
```

HAL экспортирует структуру `sdio_host_ops_t` с указателями на все функции. Это позволяет драйверу работать с любым SDIO контроллером — нужно только реализовать эту структуру.

### Уровень 3: Драйвер CYW55500 (cyw55500_sdio.c)

Драйвер знает всё о чипе CYW55500: его регистры, протоколы, как загрузить прошивку, как отправить WiFi команду.

**Backplane доступ:**

CYW55500 имеет внутреннюю шину (backplane) с несколькими ядрами:
- ChipCommon — общие настройки чипа
- SDIO Core — SDIO интерфейс
- ARM Core — процессор внутри чипа
- RAM — память для прошивки

Чтобы прочитать регистр ядра, нужно:
1. Установить окно backplane через регистры `0x1000A-0x1000C`
2. Прочитать данные через CMD53 на Function 1

```c
// Читаем 32-бит регистр по адресу backplane
cyw_err_t cyw_sdio_read32(uint32_t addr, uint32_t *val)
{
    // Устанавливаем окно
    set_backplane_window(addr);

    // Читаем через CMD53
    uint8_t data[4];
    ops->cmd53_read(SDIO_FUNC_1, addr & 0x7FFF, data, 4, true);

    *val = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    return CYW_OK;
}
```

**Протокол SDPCM:**

Вся коммуникация с прошивкой идёт через SDPCM (SDIO PCM) протокол. Каждый пакет имеет заголовок:

```
┌─────────┬───────────┬─────┬─────────┬──────────────┬─────────┐
│ len (2) │ ~len (2)  │ seq │ channel │ next_len (1) │ offset  │
└─────────┴───────────┴─────┴─────────┴──────────────┴─────────┘
```

Каналы:
- 0 — Control (IOCTL команды)
- 1 — Event (события от прошивки)
- 2 — Data (WiFi пакеты)

**Протокол BCDC:**

Поверх SDPCM работает BCDC (Broadcom Dongle Control) для IOCTL команд:

```c
// Отправляем IOCTL команду
cyw_err_t cyw_ioctl(uint32_t cmd, void *data, uint32_t len, bool set)
{
    // Формируем BCDC заголовок
    bcdc_header_t hdr = {
        .cmd = cmd,
        .len = len,
        .flags = (set ? 0x02 : 0) | (BCDC_PROTO_VER << 4),
        .status = 0
    };

    // Оборачиваем в SDPCM
    // Отправляем через CMD53 на Function 2
    // Ждём ответа
}
```

### Уровень 4: Приложение (main.c)

Приложение использует простой API драйвера:

```c
int main(void)
{
    // 1. Инициализируем драйвер
    //    Передаём указатель на HAL функции
    cyw_init(litex_get_sdio_ops());

    // 2. Загружаем прошивку в чип
    //    Прошивка записывается в RAM чипа через backplane
    cyw_load_firmware(fw_data, fw_size, nvram_data, nvram_size);

    // 3. Запускаем WiFi
    cyw_up();

    // 4. Используем WiFi
    cyw_connect("MyNetwork", "password123");

    // 5. Главный цикл
    while (1) {
        cyw_poll();  // Обрабатываем события
    }
}
```

## Инициализация SDIO карты

При включении WiFi чипа нужно выполнить инициализацию SDIO:

```
1. CMD0  (GO_IDLE)      — сброс карты
2. CMD5  (IO_SEND_OP)   — запрос OCR, проверка напряжения
3. CMD5  (IO_SEND_OP)   — с нужным напряжением, ждём ready
4. CMD3  (SEND_RCA)     — получаем относительный адрес карты
5. CMD7  (SELECT_CARD)  — выбираем карту для работы
6. CMD52 (IO_RW_DIRECT) — настраиваем 4-bit режим
```

После этого карта готова к работе и можно использовать CMD52/CMD53.

## Загрузка прошивки

CYW55500 — это не просто радио, а полноценный SoC с ARM процессором. Ему нужна прошивка:

1. **Останавливаем ARM ядро** — пишем в регистры ARM Core
2. **Загружаем прошивку** — пишем .bin файл в RAM через backplane
3. **Загружаем NVRAM** — конфигурация (страна, мощность, калибровка)
4. **Запускаем ARM** — прошивка начинает работу
5. **Ждём HMB_DATA_FWREADY** — прошивка сообщает о готовности

## Файлы прошивки

Прошивка от Infineon:
- `cyfmac55500-sdio.trxse` — бинарник (~600KB)
- `cyfmac55500-sdio.txt` — NVRAM конфигурация

NVRAM пример:
```
boardtype=0xffff
boardrev=0x1101
vendid=0x14e4
devid=0x4418
macaddr=00:11:22:33:44:55
ccode=KZ
regrev=0
```

## Сборка

### Требования

- RISC-V тулчейн: `riscv64-elf-gcc` или `riscv64-unknown-elf-gcc`
- GNU Make
- LiteX (для генерации `generated/*.ld` файлов)

### Команды

```bash
# Сборка
make

# Очистка
make clean

# Результат
wifi_firmware.elf  — ELF файл для отладки
wifi_firmware.bin  — бинарник для загрузки
wifi_firmware.hex  — Intel HEX формат
wifi_firmware.lst  — листинг ассемблера
```

### Интеграция с LiteX

Linker script использует `INCLUDE generated/regions.ld` — это файл, который генерирует LiteX при сборке SoC. Он содержит адреса памяти:

```ld
MEMORY {
    rom : ORIGIN = 0x00000000, LENGTH = 0x00020000
    sram : ORIGIN = 0x10000000, LENGTH = 0x00004000
}
```

При сборке LiteX проекта эти файлы создаются автоматически в папке `build/xxx/software/include/generated/`.

## Регистры SDIO контроллера LiteX

| Адрес | Название | Описание |
|-------|----------|----------|
| 0x80000000 | MAIN_CLK_FREQ | Частота системного клока (read-only) |
| 0x80001000 | SDIO_CLK_FREQ | Частота SDIO клока (read-only) |
| 0x80002000 | CMD_INDEX | Индекс команды (write) / индекс ответа (read) |
| 0x80003000 | CMD_ARGUMENT | Аргумент команды (write) / аргумент ответа (read) |
| 0x80004000 | DATA_BUFFER | Буфер данных (массив 32-бит слов) |
| 0x80005000 | SEND_CMD | Триггер отправки команды |
| 0x80006000 | SEND_CMD_READ_DATA | Команда + чтение данных |
| 0x80007000 | SEND_CMD_SEND_DATA | Команда + запись данных |
| 0x80008000 | READ_DATA | Только чтение данных |
| 0x80009000 | SEND_DATA | Только запись данных |
| 0x8000A000 | CMD_BUSY | Флаг занятости команды |
| 0x8000B000 | DATA_BUSY | Флаг занятости данных |
| 0x8000C000 | CMD_STATUS | Статус команды (timeout, response index) |
| 0x8000D000 | DATA_STATUS | Статус данных (CRC error, timeout) |
| 0x8000E000 | DATA_LENGTH | Длина данных в байтах |

## Регистры WiFi чипа CYW55500

Чип имеет сотни регистров. Основные группы:

**CCCR (Card Common Control Registers)** — стандартные SDIO:
- 0x00 — SDIO Revision
- 0x02 — IO Enable
- 0x03 — IO Ready
- 0x07 — Bus Interface Control

**Function 1 Misc** — Broadcom специфичные:
- 0x1000A — Backplane address low
- 0x1000B — Backplane address mid
- 0x1000C — Backplane address high
- 0x1000E — Chip Clock CSR

**ChipCommon Core** (через backplane):
- 0x000 — Chip ID
- 0x004 — Capabilities
- 0x1E0 — Clock Control Status

Полный список в `cyw55500_regs.h`.

## Отладка

### UART вывод

Реализуйте функции в `main.c`:
```c
extern void uart_puts(const char *s);
extern void uart_puthex(uint32_t val);
```

### Debug флаг

В Makefile:
```makefile
CFLAGS += -DCYW_DEBUG=1
```

### Проверка связи с чипом

```c
// Читаем Chip ID — должен быть 0x55500xxx
uint32_t chip_id;
cyw_sdio_read32(0x18000000, &chip_id);
printf("Chip ID: 0x%08X\n", chip_id);
```

## Ограничения

Этот драйвер — базовая реализация. Не включено:

1. **WPA/WPA2 Supplicant** — нужен для защищённых сетей
2. **TCP/IP стек** — нужен lwIP или подобный
3. **Полная обработка событий** — только базовый парсинг
4. **Power Management** — режимы сна чипа
5. **AP режим** — только Station режим

