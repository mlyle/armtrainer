ENTRY(student_main)

MEMORY
{
	RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 48K
}

SECTIONS
{
	. = ORIGIN(RAM);
	.text : ALIGN(4)
	{
		KEEP(*(.text.student_main))
		*(.text*)
		*(.rodata*)
	} > RAM

	.data : ALIGN(4)
	{
		*(.data*)
	} > RAM

	.bss (NOLOAD) : ALIGN(4)
	{
		*(.bss*)
		*(COMMON)
	} > RAM

	/DISCARD/ :
	{
		*(.ARM.attributes)
		*(.ARM.exidx*)
		*(.ARM.extab*)
		*(.comment)
	}
}
