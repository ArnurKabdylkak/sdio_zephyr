#============================================================================
# CYW55500 WiFi Bare-metal Driver - Makefile
#
# For RISC-V (LiteX) SoC
#============================================================================

# LiteX build directory (update this path to your LiteX build)
LITEX_BUILD ?= ../build/sipeed_tang_primer_20k

# Cross-compiler prefix
CROSS_COMPILE ?= riscv64-elf-
#CROSS_COMPILE ?= riscv64-unknown-elf-
#CROSS_COMPILE ?= riscv32-unknown-elf-

CC      = $(CROSS_COMPILE)gcc
LD      = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size

# Target name
TARGET = wifi_firmware

# RISC-V architecture flags
# Adjust based on your CPU configuration
ARCH_FLAGS = -march=rv32imac_zicsr -mabi=ilp32
#ARCH_FLAGS = -march=rv64imac_zicsr -mabi=lp64

# LiteX generated includes
LITEX_INC = $(LITEX_BUILD)/software/include

# Compiler flags
CFLAGS  = $(ARCH_FLAGS)
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -O2 -g
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -ffreestanding -nostdlib -fno-builtin
CFLAGS += -I.
CFLAGS += -I$(LITEX_INC)
CFLAGS += -DCYW_DEBUG=0
CFLAGS += -DUSE_EMBEDDED_FW=1

# Linker flags
LDFLAGS  = $(ARCH_FLAGS)
LDFLAGS += -nostdlib
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -L$(LITEX_INC)/generated
LDFLAGS += -T linker.ld

# Source files
SRCS = main.c \
       cyw55500_sdio.c \
       sdio_litex.c \
       libc.c

# Assembly files
ASRCS = startup.S

# Object files
OBJS = $(SRCS:.c=.o) $(ASRCS:.S=.o)

# Firmware files - embed into binary
FW_BIN = cyfmac55500-sdio.trxse
NVRAM_TXT = cyfmac55500-sdio.txt
FW_OBJS = fw_cyfmac55500.o nvram_cyfmac55500.o

#============================================================================
# Build Rules
#============================================================================

.PHONY: all clean flash load sim size check-litex

all: check-litex $(TARGET).elf $(TARGET).bin $(TARGET)_fw.bin $(TARGET).hex size

$(TARGET).elf: $(OBJS) $(FW_OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(FW_OBJS) -lgcc
	$(OBJDUMP) -d $@ > $(TARGET).lst

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary -j .text -j .rodata $< $@

$(TARGET)_fw.bin: $(TARGET).elf
	$(OBJCOPY) -O binary -j .firmware $< $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

# Embed firmware binary
fw_cyfmac55500.o: $(FW_BIN)
	$(OBJCOPY) -I binary -O elf32-littleriscv -B riscv \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_cyfmac55500_sdio_trxse_start=_binary_cyfmac55500_sdio_bin_start \
		--redefine-sym _binary_cyfmac55500_sdio_trxse_end=_binary_cyfmac55500_sdio_bin_end \
		--redefine-sym _binary_cyfmac55500_sdio_trxse_size=_binary_cyfmac55500_sdio_bin_size \
		$< $@

# Embed NVRAM
nvram_cyfmac55500.o: $(NVRAM_TXT)
	$(OBJCOPY) -I binary -O elf32-littleriscv -B riscv \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_cyfmac55500_sdio_txt_start=_binary_cyfmac55500_sdio_txt_start \
		--redefine-sym _binary_cyfmac55500_sdio_txt_end=_binary_cyfmac55500_sdio_txt_end \
		--redefine-sym _binary_cyfmac55500_sdio_txt_size=_binary_cyfmac55500_sdio_txt_size \
		$< $@

size: $(TARGET).elf
	$(SIZE) $<

clean:
	rm -f $(OBJS) $(FW_OBJS)
	rm -f $(TARGET).elf $(TARGET).bin $(TARGET)_fw.bin $(TARGET).hex $(TARGET).lst

#============================================================================
# LiteX Integration
#============================================================================

# Check if LiteX generated files exist
check-litex:
	@if [ ! -d "$(LITEX_INC)/generated" ]; then \
		echo "ERROR: LiteX generated files not found!"; \
		echo "Expected path: $(LITEX_INC)/generated"; \
		echo ""; \
		echo "Please build LiteX first:"; \
		echo "  python3 sipeed_tang_primer_20k.py --build"; \
		echo ""; \
		echo "Or set LITEX_BUILD to your build directory:"; \
		echo "  make LITEX_BUILD=/path/to/build/sipeed_tang_primer_20k"; \
		exit 1; \
	fi
	@echo "LiteX generated files found at $(LITEX_INC)/generated"

# Flash to device using litex_term
flash: $(TARGET).bin
	litex_term --kernel $(TARGET).bin /dev/ttyUSB0

# Load via serial
load: $(TARGET).bin
	litex_term --kernel $(TARGET).bin /dev/ttyUSB0

# Run with LiteX simulation
sim: $(TARGET).bin
	@echo "Running in simulation..."
	# lxsim --rom $(TARGET).bin
