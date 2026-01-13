# SPDX-License-Identifier: Apache-2.0
#
# Board configuration for LiteX VexRiscv with SDIO
#

# Use generic LiteX runner for flashing
# This can be customized based on your loading method
board_runner_args(misc-flasher "--cmd-load=litex_term --kernel {bin_file} /dev/ttyUSB1")
board_runner_args(misc-flasher "--cmd-flash=echo 'Flashing not supported, use --cmd-load'")

include(${ZEPHYR_BASE}/boards/common/misc-flasher.board.cmake)
