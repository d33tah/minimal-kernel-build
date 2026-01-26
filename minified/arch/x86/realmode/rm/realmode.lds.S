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
		. = ALIGN(16);
		video_cards = .;
		*(.videocards)
		video_cards_end = .;
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

	.text64 : {
		*(.text64)
		*(.text64.*)
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

	/* End signature for integrity checking */
	. = ALIGN(4);
	.signature : {
		*(.signature)
	}

	/DISCARD/ : {
		*(.note*)
		*(.debug*)
	}

/* --- 2026-01-26 01:55 --- Inlined from pasyms.h */
pa_HEAP = HEAP;
pa_end_signature = end_signature;
pa_heap_end = heap_end;
pa_machine_real_restart_asm = machine_real_restart_asm;
pa_machine_real_restart_gdt = machine_real_restart_gdt;
pa_machine_real_restart_idt = machine_real_restart_idt;
pa_real_mode_header = real_mode_header;
pa_rm_heap = rm_heap;
pa_rm_stack = rm_stack;
pa_rm_stack_end = rm_stack_end;
pa_startup_32 = startup_32;
pa_trampoline_header = trampoline_header;
pa_trampoline_start = trampoline_start;
/* end pasyms.h */
}
