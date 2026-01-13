# Makefile for SDIO HAL

# Toolchain configuration
CROSS_COMPILE ?= riscv64-unknown-elf-
CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE = $(CROSS_COMPILE)size

# Architecture flags (adjust for your LiteX CPU)
ARCH_FLAGS = -march=rv32i -mabi=ilp32

# Compiler flags
CFLAGS = $(ARCH_FLAGS) -O2 -g -Wall -Wextra
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -I.

# Linker flags
LDFLAGS = $(ARCH_FLAGS) -Wl,--gc-sections

# Source files
HAL_SOURCES = sdio_hal.c
EXAMPLE_SOURCES = sdio_example.c
ADVANCED_EXAMPLE_SOURCES = sdio_advanced_example.c

# Object files
HAL_OBJECTS = $(HAL_SOURCES:.c=.o)
EXAMPLE_OBJECTS = $(EXAMPLE_SOURCES:.c=.o)
ADVANCED_EXAMPLE_OBJECTS = $(ADVANCED_EXAMPLE_SOURCES:.c=.o)

# Targets
LIB_TARGET = libsdio.a
EXAMPLE_TARGET = sdio_example.elf
ADVANCED_EXAMPLE_TARGET = sdio_advanced_example.elf

.PHONY: all clean lib example advanced_example examples

all: lib examples

examples: example advanced_example

# Build HAL library
lib: $(LIB_TARGET)

$(LIB_TARGET): $(HAL_OBJECTS)
	@echo "Creating library $@"
	$(AR) rcs $@ $^

# Build example
example: $(EXAMPLE_TARGET)

$(EXAMPLE_TARGET): $(HAL_OBJECTS) $(EXAMPLE_OBJECTS)
	@echo "Linking $@"
	$(CC) $(LDFLAGS) -o $@ $^
	$(SIZE) $@

# Build advanced example
advanced_example: $(ADVANCED_EXAMPLE_TARGET)

$(ADVANCED_EXAMPLE_TARGET): $(HAL_OBJECTS) $(ADVANCED_EXAMPLE_OBJECTS)
	@echo "Linking $@"
	$(CC) $(LDFLAGS) -o $@ $^
	$(SIZE) $@

# Compile C files
%.o: %.c sdio_hal.h
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Generate binary from ELF
%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

# Clean build artifacts
clean:
	rm -f *.o *.elf *.bin *.a

# Install library (optional)
install: $(LIB_TARGET)
	@echo "Installing library and headers"
	install -d $(DESTDIR)/usr/local/lib
	install -d $(DESTDIR)/usr/local/include
	install -m 644 $(LIB_TARGET) $(DESTDIR)/usr/local/lib/
	install -m 644 sdio_hal.h $(DESTDIR)/usr/local/include/

# Help
help:
	@echo "SDIO HAL Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all             - Build library and all examples (default)"
	@echo "  lib             - Build HAL library only"
	@echo "  examples        - Build all example programs"
	@echo "  example         - Build basic example program"
	@echo "  advanced_example - Build advanced example program"
	@echo "  clean           - Remove build artifacts"
	@echo "  install         - Install library and headers"
	@echo ""
	@echo "Configuration:"
	@echo "  CROSS_COMPILE - Toolchain prefix (default: riscv64-unknown-elf-)"
	@echo "  ARCH_FLAGS    - CPU architecture flags"
