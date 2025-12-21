
#ifndef _ASM_X86_SECTIONS_H
#define _ASM_X86_SECTIONS_H

#define arch_is_kernel_initmem_freed arch_is_kernel_initmem_freed

/* Inlined from asm-generic/sections.h */
#include <linux/compiler.h>
#include <linux/types.h>

extern char _text[], _stext[], _etext[];
extern char _data[], _sdata[], _edata[];
extern char __bss_start[], __bss_stop[];
extern char __init_begin[], __init_end[];
extern char _sinittext[], _einittext[];
extern char __start_ro_after_init[], __end_ro_after_init[];
extern char _end[];
extern char __per_cpu_load[], __per_cpu_start[], __per_cpu_end[];
extern char __kprobes_text_start[], __kprobes_text_end[];
extern char __entry_text_start[], __entry_text_end[];
extern char __start_rodata[], __end_rodata[];
extern char __irqentry_text_start[], __irqentry_text_end[];
extern char __softirqentry_text_start[], __softirqentry_text_end[];
extern char __start_once[], __end_once[];

extern char __ctors_start[], __ctors_end[];

extern char __start_opd[], __end_opd[];

extern char __noinstr_text_start[], __noinstr_text_end[];

extern __visible const void __nosave_begin, __nosave_end;

#define dereference_function_descriptor(p) ((void *)(p))
#define dereference_kernel_function_descriptor(p) ((void *)(p))

typedef struct {
	unsigned long addr;
} func_desc_t;

static inline bool is_kernel_rodata(unsigned long addr)
{
	return addr >= (unsigned long)__start_rodata &&
	       addr < (unsigned long)__end_rodata;
}

static inline bool is_kernel_inittext(unsigned long addr)
{
	return addr >= (unsigned long)_sinittext &&
	       addr < (unsigned long)_einittext;
}

static inline bool __is_kernel_text(unsigned long addr)
{
	return addr >= (unsigned long)_stext &&
	       addr < (unsigned long)_etext;
}

#include <asm/extable.h>

extern char __brk_base[], __brk_limit[];
extern char __end_rodata_aligned[];

extern char __end_of_kernel_reserve[];

extern unsigned long _brk_start, _brk_end;

static inline bool arch_is_kernel_initmem_freed(unsigned long addr)
{

	if (_brk_start)
		return 0;


	return addr >= _brk_end && addr < (unsigned long)&_end;
}

#endif
