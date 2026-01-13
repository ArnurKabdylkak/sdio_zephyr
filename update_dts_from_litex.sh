#!/bin/bash
#
# Скрипт для обновления Device Tree адресов из LiteX CSR
#
# Использование:
#   1. Соберите LiteX: cd litex && python3 sipeed_tang_primer_20k.py --build
#   2. Запустите скрипт: ./update_dts_from_litex.sh
#

set -e

LITEX_BUILD="litex/build"
CSR_CSV="$LITEX_BUILD/csr.csv"
CSR_H="$LITEX_BUILD/software/include/generated/csr.h"
DTS_FILE="app/boards/litex_vexriscv_sdio.dts"

echo "=== LiteX to Zephyr DTS Address Updater ==="
echo ""

# Проверка наличия файлов
if [ ! -f "$CSR_CSV" ]; then
    echo "ERROR: $CSR_CSV not found!"
    echo "Please build LiteX first:"
    echo "  cd litex/"
    echo "  python3 sipeed_tang_primer_20k.py --build --csr-csv=build/csr.csv"
    exit 1
fi

if [ ! -f "$DTS_FILE" ]; then
    echo "ERROR: $DTS_FILE not found!"
    exit 1
fi

echo "Reading addresses from LiteX build..."
echo ""

# Функция для извлечения адреса из CSV
get_address() {
    local name=$1
    grep "csr_base,$name," "$CSR_CSV" | cut -d, -f3 | tr -d ' '
}

# Извлекаем адреса
UART_ADDR=$(get_address "uart")
TIMER_ADDR=$(get_address "timer0")
SDIO_ADDR=$(get_address "my_slave")

# Если не нашли в CSV, пробуем из csr.h
if [ -z "$UART_ADDR" ] && [ -f "$CSR_H" ]; then
    UART_ADDR=$(grep "CSR_UART_BASE" "$CSR_H" | awk '{print $3}' | sed 's/L$//')
fi

if [ -z "$TIMER_ADDR" ] && [ -f "$CSR_H" ]; then
    TIMER_ADDR=$(grep "CSR_TIMER0_BASE" "$CSR_H" | awk '{print $3}' | sed 's/L$//')
fi

if [ -z "$SDIO_ADDR" ] && [ -f "$CSR_H" ]; then
    SDIO_ADDR=$(grep "CSR_MY_SLAVE_BASE" "$CSR_H" | awk '{print $3}' | sed 's/L$//')
fi

# Выводим найденные адреса
echo "Found addresses:"
echo "  UART:   ${UART_ADDR:-NOT FOUND}"
echo "  TIMER:  ${TIMER_ADDR:-NOT FOUND}"
echo "  SDIO:   ${SDIO_ADDR:-NOT FOUND}"
echo ""

# Проверяем SDIO адрес
if [ "$SDIO_ADDR" != "0x80000000" ]; then
    echo "WARNING: SDIO address is not 0x80000000!"
    echo "Expected: 0x80000000"
    echo "Found:    $SDIO_ADDR"
    echo ""
    echo "This might be intentional if you changed sipeed_tang_primer_20k.py"
    echo "Update will proceed with new address."
    echo ""
fi

# Создаем backup
BACKUP="${DTS_FILE}.backup.$(date +%Y%m%d_%H%M%S)"
cp "$DTS_FILE" "$BACKUP"
echo "Created backup: $BACKUP"
echo ""

# Обновляем адреса в DTS
if [ -n "$UART_ADDR" ]; then
    echo "Updating UART address to $UART_ADDR..."
    sed -i "s/uart0: serial@[0-9a-fx]*/uart0: serial@${UART_ADDR}/" "$DTS_FILE"
    sed -i "s/\(uart0.*reg = <\)0x[0-9a-fA-F]*/\1${UART_ADDR}/" "$DTS_FILE"
fi

if [ -n "$TIMER_ADDR" ]; then
    echo "Updating TIMER address to $TIMER_ADDR..."
    sed -i "s/timer0: timer@[0-9a-fx]*/timer0: timer@${TIMER_ADDR}/" "$DTS_FILE"
    sed -i "s/\(timer0.*reg = <\)0x[0-9a-fA-F]*/\1${TIMER_ADDR}/" "$DTS_FILE"
fi

if [ -n "$SDIO_ADDR" ]; then
    echo "Updating SDIO address to $SDIO_ADDR..."
    sed -i "s/sdio0: sdio@[0-9a-fx]*/sdio0: sdio@${SDIO_ADDR}/" "$DTS_FILE"
    sed -i "s/\(sdio0.*reg = <\)0x[0-9a-fA-F]* 0x10000/\1${SDIO_ADDR} 0x10000/" "$DTS_FILE"
fi

echo ""
echo "=== Update Complete ==="
echo ""
echo "Updated: $DTS_FILE"
echo "Backup:  $BACKUP"
echo ""
echo "Next steps:"
echo "  1. Review changes: diff $BACKUP $DTS_FILE"
echo "  2. Build Zephyr: cd app && west build -b litex_vexriscv_sdio ."
echo ""
