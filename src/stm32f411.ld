_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
	/* Start of program at beginning of flash */
	.vectors :
	{
		. = ALIGN(4);
		_start_vectors = .;
		KEEP(*(.vectors))
		KEEP(*(.systick_vector))
		KEEP(*(.interrupt_vectors))
		. = ALIGN(4);
	} >FLASH

	.text :
	{
		. = ALIGN(4);
		*(.text) *(.text.*)
		*(.glue_7t) *(.glue_7)
		. = ALIGN(4);
	} >FLASH

	.rodata :
	{
		. = ALIGN(4);
		*(.rodata) *(.rodata*)
		. = ALIGN(4);
	} >FLASH

	_sidata = LOADADDR(.data);

	.data :
	{
		. = ALIGN(4);
		_sdata = .;
		*(.data) *(.data*)
		. = ALIGN(4);
		_edata = .;
	} >RAM AT> FLASH

	.fill LOADADDR(.data) + (_edata - _sdata) :
	{
		FILL(0xffffffff);
		. = ORIGIN(FLASH) + LENGTH(FLASH) - 1;
		BYTE(0xff)
		_efill = .;
	}

	.bss :
	{
		. = ALIGN(4);
		_sbss = .;
		*(.bss) *(.bss.*)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;
	} >RAM

	.userram :
	{
		. = ORIGIN(USERRAM);
		_suserram = .;
		. = ORIGIN(USERRAM)+LENGTH(USERRAM);
		_euserram = .;
	} >USERRAM
}
