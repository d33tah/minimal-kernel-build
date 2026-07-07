/* SPDX-License-Identifier: GPL-2.0 */
/*
 * realmode.lds.S
 *
 * Linker script for the real-mode code
 */

#include <asm/page_types.h>

#undef i386

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(pa_text_start)

SECTIONS
{
	real_mode_seg = 0;

	. = 0;
	.header : {
		pa_real_mode_base = .;
		*(.header)
	}

	. = ALIGN(4);
	.rodata : {
		*(.rodata)
		*(.rodata.*)
	}

	. = ALIGN(PAGE_SIZE);
	pa_text_start = .;
	.text : {
		*(.text)
		*(.text.*)
	}

	.text32 : {
		*(.text32)
		*(.text32.*)
	}
	pa_ro_end = .;

	. = ALIGN(PAGE_SIZE);
	.data : {
		*(.data)
		*(.data.*)
	}

	. = ALIGN(128);
	.bss : {
		*(.bss*)
	}

	/DISCARD/ : {
		*(.note*)
		*(.debug*)
	}

#include "pasyms.h"
}
