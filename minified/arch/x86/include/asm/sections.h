
#ifndef _ASM_X86_SECTIONS_H
#define _ASM_X86_SECTIONS_H

/* Inlined from asm-generic/sections.h */
#include <linux/compiler.h>
#include <linux/types.h>

extern char _text[], _stext[], _etext[];
extern char _data[], _sdata[], _edata[];
extern char __bss_start[], __bss_stop[];
extern char __init_begin[], __init_end[];
extern char _end[];
extern char __per_cpu_load[];
/* __kprobes_text_*, __entry_text_*, __softirqentry_text_*, __start_once, __end_once, __noinstr_text_*, __nosave_* removed - unused */
extern char __start_rodata[], __end_rodata[];
extern char __irqentry_text_start[], __irqentry_text_end[];

/* dereference_function_descriptor, dereference_kernel_function_descriptor, func_desc_t removed - unused */

static inline bool is_kernel_rodata(unsigned long addr)
{
	return addr >= (unsigned long)__start_rodata &&
	       addr < (unsigned long)__end_rodata;
}

#include <asm/extable.h>

extern char __brk_base[], __brk_limit[];
extern char __end_rodata_aligned[];

extern char __end_of_kernel_reserve[];

extern unsigned long _brk_start, _brk_end;

#endif
