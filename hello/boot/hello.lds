/*
 * Flat image loaded at code32_start; startup_32 must come first.
 * KERNEL_BASE comes from the Makefile (--defsym).
 */
ENTRY(startup_32)
SECTIONS
{
	. = KERNEL_BASE;
	.text : { *(.text.head) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	.data : { *(.data .data.*) }
	.bss : { *(.bss .bss.* COMMON) }
	_image_size = ABSOLUTE(. - KERNEL_BASE);	/* becomes init_size */
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
