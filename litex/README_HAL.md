# SDIO HAL для LiteX

Минимальный Hardware Abstraction Layer (HAL) для SDIO контроллера на базе HDL имплементации.

## Архитектура

### HDL модули
- **WishboneController.sv** - Wishbone slave интерфейс с memory-mapped регистрами
- **SDCommandController.sv** - Контроллер SDIO команд (CMD линия)
- **SDDataController.sv** - Контроллер SDIO данных (4-битная DAT шина)

### HAL компоненты
- **sdio_hal.h** - Заголовочный файл с API и определениями
- **sdio_hal.c** - Реализация HAL функций
- **sdio_example.c** - Пример использования для инициализации SDIO WiFi модуля

## Memory Map

| Offset | Регистр | Описание |
|--------|---------|----------|
| 0x0000 | MAIN_CLOCK_FREQ | Частота системной шины (Гц) |
| 0x1000 | SD_CLOCK_FREQ | Частота SDIO clock (Гц) |
| 0x2000 | CMD_INDEX | Индекс команды [5:0] |
| 0x3000 | CMD_ARGUMENT | Аргумент команды/ответа (4x32bit) |
| 0x4000 | DATA_BUFFER | Буфер данных (512x32bit = 2KB) |
| 0x5000 | SEND_CMD_OP | Операция: отправить команду |
| 0x6000 | SEND_CMD_READ_DATA_OP | Операция: команда + чтение данных |
| 0x7000 | SEND_CMD_SEND_DATA_OP | Операция: команда + запись данных |
| 0x8000 | READ_DATA_OP | Операция: только чтение данных |
| 0x9000 | SEND_DATA_OP | Операция: только запись данных |
| 0xA000 | CMD_BUSY | Флаг занятости CMD контроллера |
| 0xB000 | DATA_BUSY | Флаг занятости DATA контроллера |
| 0xC000 | CMD_STATUS | Статус команды (timeout, index) |
| 0xD000 | DATA_STATUS | Статус данных (error, timeout) |
| 0xE000 | DATA_LENGTH | Длина данных в байтах [10:0] |

## API функции

### Инициализация

```c
void sdio_init(uint32_t main_clk_freq, uint32_t sd_clk_freq);
```
Инициализирует SDIO контроллер с заданными частотами.

**Параметры:**
- `main_clk_freq` - частота системной шины в Гц (например, 48000000)
- `sd_clk_freq` - начальная частота SDIO clock в Гц (100000 для инициализации)

**Пример:**
```c
sdio_init(48000000, 100000);  // 48MHz system, 100kHz SDIO
```

### Управление частотой

```c
void sdio_set_clock_freq(uint32_t sd_clk_freq);
uint32_t sdio_get_clock_freq(void);
```

Устанавливает/читает частоту SDIO clock. Обычно начинают с 100-400kHz для инициализации, потом переключают на 25MHz или 50MHz для передачи данных.

### Отправка команд

```c
sdio_status_t sdio_send_cmd(uint8_t cmd_index, uint32_t arg,
                             sdio_response_t *resp);
```

Отправляет SDIO команду и получает ответ.

**Параметры:**
- `cmd_index` - индекс команды (0-63)
- `arg` - 32-битный аргумент команды
- `resp` - указатель на структуру для ответа (может быть NULL)

**Возвращает:**
- `SDIO_OK` - успех
- `SDIO_ERROR_TIMEOUT` - таймаут команды
- `SDIO_ERROR_BUSY` - контроллер занят

**Пример:**
```c
sdio_response_t resp;
sdio_status_t status = sdio_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, &resp);
if (status == SDIO_OK && !resp.timeout) {
    printf("CMD0 OK, response: 0x%08x\n", resp.arg[0]);
}
```

### Команда с чтением данных

```c
sdio_status_t sdio_send_cmd_with_data_read(uint8_t cmd_index, uint32_t arg,
                                            uint32_t *data_buf, uint16_t data_len,
                                            sdio_response_t *resp);
```

Отправляет команду и читает данные (например CMD53).

**Параметры:**
- `cmd_index` - индекс команды
- `arg` - аргумент команды
- `data_buf` - буфер для данных (выровненный по 32-бит)
- `data_len` - длина данных в байтах (max 2048)
- `resp` - ответ команды

**Пример:**
```c
uint32_t buf[128];  // 512 bytes
sdio_send_cmd_with_data_read(SD_CMD53_IO_RW_EXTENDED, cmd53_arg,
                               buf, 512, &resp);
```

### Команда с записью данных

```c
sdio_status_t sdio_send_cmd_with_data_write(uint8_t cmd_index, uint32_t arg,
                                             const uint32_t *data_buf, uint16_t data_len,
                                             sdio_response_t *resp);
```

Аналогично чтению, но записывает данные.

### Операции только с данными

```c
sdio_status_t sdio_read_data(uint32_t *data_buf, uint16_t data_len);
sdio_status_t sdio_write_data(const uint32_t *data_buf, uint16_t data_len);
```

Чтение/запись данных без отправки команды (для multi-block операций).

### Проверка статуса

```c
bool sdio_is_cmd_busy(void);
bool sdio_is_data_busy(void);
void sdio_wait_cmd_ready(void);
void sdio_wait_data_ready(void);
```

## Типовая последовательность инициализации SDIO WiFi

```c
sdio_response_t resp;
sdio_status_t status;

// 1. Инициализация с низкой частотой
sdio_init(48000000, 100000);

// 2. CMD0: Reset
sdio_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, &resp);

// 3. CMD5: Проверка SDIO (OCR)
sdio_send_cmd(SD_CMD5_IO_SEND_OP_COND, 0, &resp);
uint32_t ocr = resp.arg[0];

// 4. CMD5: Установка напряжения
sdio_send_cmd(SD_CMD5_IO_SEND_OP_COND, 0x00300000, &resp);

// 5. CMD3: Получение RCA
sdio_send_cmd(SD_CMD3_SEND_RELATIVE_ADDR, 0, &resp);
uint16_t rca = (resp.arg[0] >> 16) & 0xFFFF;

// 6. CMD7: Выбор карты
sdio_send_cmd(SD_CMD7_SELECT_CARD, rca << 16, &resp);

// 7. Повышение частоты для передачи данных
sdio_set_clock_freq(25000000);

// 8. CMD52: Настройка регистров CCCR/FBR
uint32_t cmd52_arg = (1 << 31) |  // Write
                     (0 << 28) |  // Function 0
                     (addr << 9) |
                     data;
sdio_send_cmd(SD_CMD52_IO_RW_DIRECT, cmd52_arg, &resp);

// 9. CMD53: Передача данных
uint32_t cmd53_arg = (0 << 31) |     // Read
                     (1 << 28) |     // Function 1
                     (1 << 27) |     // Block mode
                     (1 << 26) |     // Auto-increment
                     (addr << 9) |
                     block_count;
uint32_t data[128];
sdio_send_cmd_with_data_read(SD_CMD53_IO_RW_EXTENDED, cmd53_arg,
                               data, 512, &resp);
```

## Особенности HDL имплементации

1. **Генерация clock**: SDIO clock генерируется делением системной частоты:
   ```
   sd_clk = main_clk / (2 * divider)
   divider = MAIN_CLOCK_FREQUENCY / SD_CLOCK_FREQUENCY / 2
   ```

2. **Dual-port RAM**: Буфер данных имеет независимые порты чтения/записи для работы в разных clock доменах (system clock и SDIO clock).

3. **CRC**:
   - CMD линия: CRC7
   - DAT линии: CRC16 для каждой из 4 линий

4. **Response types**:
   - Короткий ответ: 48 бит (R1, R3, R4, R5, R6, R7)
   - Длинный ответ: 136 бит (R2 - для CMD2, CMD9, CMD10)

5. **Операции triggered by read**: Операционные регистры (0x5000-0x9000) запускают операцию при чтении из них.

## Компиляция примера

```bash
# Для bare-metal на LiteX SoC:
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 \
    -c sdio_hal.c -o sdio_hal.o

riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 \
    -c sdio_example.c -o sdio_example.o

riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 \
    sdio_hal.o sdio_example.o -o sdio_example.elf
```

## Поддержка отладки

HAL предоставляет доступ к отладочным выводам через `sd_debug[3:0]`:
- `sd_debug[0]` - SDIO clock
- `sd_debug[1]` - SDIO command
- `sd_debug[2]` - SDIO data[0]
- `sd_debug[3]` - SDIO data[1]

Эти сигналы можно наблюдать логическим анализатором для отладки.

## Ограничения

- Максимальный размер буфера данных: 2048 байт
- Поддержка только 4-битного режима передачи данных
- Timeout в polling режиме (нет поддержки прерываний)
- Базовый адрес 0x80000000 из конфигурации SoC

## Дальнейшее развитие

Для полноценной работы с WiFi модулем (ESP32-C3, RTL8720, etc.) потребуется:

1. Драйвер верхнего уровня для конкретного чипа
2. Поддержка прерываний вместо polling
3. DMA для больших передач данных
4. Поддержка различных режимов питания
5. Обработка SDIO interrupts от WiFi модуля
