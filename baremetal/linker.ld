/* Include paths are relative to -L directory */
INCLUDE output_format.ld
ENTRY(_start)

INCLUDE regions.ld

SECTIONS
{
	.text :
	{
		_ftext = .;
		_text_start = .;
		KEEP(*(.text.start))
		KEEP(*(.isr_vector))
		*crt0*(.text)
		KEEP(*crt0*(.text))
		KEEP(*(.text.isr))

		*(.text .stub .text.* .gnu.linkonce.t.*)
		_etext = .;
		_text_end = .;
	} > rom

	.rodata :
	{
		. = ALIGN(8);
		_frodata = .;
		EXCLUDE_FILE(*fw_cyfmac55500.o *nvram_cyfmac55500.o) *(.rodata .rodata.* .gnu.linkonce.r.*)
		*(.rodata1)
		*(.got .got.*)
		*(.toc .toc.*)

		FILL(0);
		. = ALIGN(8);
		_erodata = .;
	} > rom

	/* Firmware data in DDR RAM (too large for ROM) */
	.firmware :
	{
		. = ALIGN(8);
		_firmware_start = .;
		*fw_cyfmac55500.o(.rodata .rodata.*)
		*nvram_cyfmac55500.o(.rodata .rodata.*)
		. = ALIGN(8);
		_firmware_end = .;
	} > main_ram

	.data :
	{
		. = ALIGN(8);
		_fdata = .;
		_data_start = .;
		*(.data .data.* .gnu.linkonce.d.*)
		*(.data1)
		*(.sdata .sdata.* .gnu.linkonce.s.*)

		FILL(0);
		. = ALIGN(8);
		_edata = .;
		_data_end = .;
	} > sram AT > rom

	.bss :
	{
		. = ALIGN(8);
		_fbss = .;
		_bss_start = .;
		*(.dynsbss)
		*(.sbss .sbss.* .gnu.linkonce.sb.*)
		*(.scommon)
		*(.dynbss)
		*(.bss .bss.* .gnu.linkonce.b.*)
		*(COMMON)
		. = ALIGN(8);
		_ebss = .;
		_bss_end = .;
		_end = .;
	} > sram

	/DISCARD/ :
	{
		*(.eh_frame)
		*(.comment)
		*(.note.*)
	}
}

PROVIDE(_fstack = ORIGIN(sram) + LENGTH(sram));
PROVIDE(_stack_top = _fstack);

PROVIDE(_fdata_rom = LOADADDR(.data));
PROVIDE(_edata_rom = LOADADDR(.data) + SIZEOF(.data));
PROVIDE(_data_load = LOADADDR(.data));

PROVIDE(__global_pointer$ = _data_start + 0x800);
