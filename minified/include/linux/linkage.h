#ifndef _LINUX_LINKAGE_H
#define _LINUX_LINKAGE_H

#include <linux/compiler_types.h>
#include <linux/stringify.h>
/* export.h inlined */
#ifndef __ASSEMBLY__
#define THIS_MODULE ((struct module *)0)
#define __EXPORT_SYMBOL(sym, sec, ns)
#define _EXPORT_SYMBOL(sym, sec)	__EXPORT_SYMBOL(sym, sec, "")
#define EXPORT_SYMBOL(sym)		_EXPORT_SYMBOL(sym, "")
#endif
#include <asm/ibt.h>

#undef notrace
#define notrace __attribute__((no_instrument_function))

#define asmlinkage CPP_ASMLINKAGE __attribute__((regparm(0)))

#ifdef __ASSEMBLY__

/* X86_32 without X86_ALIGNMENT_16 - use default alignment */

#else

#endif

#define SYM_FUNC_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)	\
	ENDBR

#define SYM_FUNC_START_NOALIGN(name)			\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_NONE)	\
	ENDBR

#define SYM_FUNC_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)	\
	ENDBR

#define SYM_FUNC_START_LOCAL_NOALIGN(name)		\
	SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)	\
	ENDBR

#ifndef ASM_NL
#define ASM_NL		 ;
#endif

#define CPP_ASMLINKAGE

#ifndef asmlinkage
#define asmlinkage
#endif


#define __page_aligned_bss	__section(".bss..page_aligned") __aligned(PAGE_SIZE)

#define __PAGE_ALIGNED_BSS	.section ".bss..page_aligned", "aw"

#ifndef __ASSEMBLY__
#ifndef asmlinkage_protect
# define asmlinkage_protect(n, ret, args...)	do { } while (0)
#endif
#endif

#ifndef __ALIGN
#define __ALIGN .align 4, 0x90
#endif

#ifdef __ASSEMBLY__

#ifndef SYM_T_FUNC
#define SYM_T_FUNC				STT_FUNC
#endif

#ifndef SYM_T_OBJECT
#define SYM_T_OBJECT				STT_OBJECT
#endif

#ifndef SYM_T_NONE
#define SYM_T_NONE				STT_NOTYPE
#endif

#define SYM_A_ALIGN				ALIGN
#define SYM_A_NONE				 

#define SYM_L_GLOBAL(name)			.globl name
#define SYM_L_LOCAL(name)			 

#ifndef LINKER_SCRIPT
#define ALIGN __ALIGN
#endif  

#ifndef SYM_ENTRY
#define SYM_ENTRY(name, linkage, align...)		\
	linkage(name) ASM_NL				\
	align ASM_NL					\
	name:
#endif

#ifndef SYM_START
#define SYM_START(name, linkage, align...)		\
	SYM_ENTRY(name, linkage, align)
#endif

#ifndef SYM_END
#define SYM_END(name, sym_type)				\
	.type name sym_type ASM_NL			\
	.set .L__sym_size_##name, .-name ASM_NL		\
	.size name, .L__sym_size_##name
#endif


#ifndef SYM_INNER_LABEL
#define SYM_INNER_LABEL(name, linkage)		\
	.type name SYM_T_NONE ASM_NL			\
	SYM_ENTRY(name, linkage, SYM_A_NONE)
#endif

#ifndef SYM_FUNC_END
#define SYM_FUNC_END(name)				\
	SYM_END(name, SYM_T_FUNC)
#endif

#ifndef SYM_CODE_START
#define SYM_CODE_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)
#endif

#ifndef SYM_CODE_START_LOCAL
#define SYM_CODE_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)
#endif

#ifndef SYM_CODE_START_LOCAL_NOALIGN
#define SYM_CODE_START_LOCAL_NOALIGN(name)		\
	SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)
#endif

#ifndef SYM_CODE_END
#define SYM_CODE_END(name)				\
	SYM_END(name, SYM_T_NONE)
#endif

#ifndef SYM_DATA_START
#define SYM_DATA_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_NONE)
#endif

#ifndef SYM_DATA_START_LOCAL
#define SYM_DATA_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)
#endif

#ifndef SYM_DATA_END
#define SYM_DATA_END(name)				\
	SYM_END(name, SYM_T_OBJECT)
#endif

#ifndef SYM_DATA_END_LABEL
#define SYM_DATA_END_LABEL(name, linkage, label)	\
	linkage(label) ASM_NL				\
	.type label SYM_T_OBJECT ASM_NL			\
	label:						\
	SYM_END(name, SYM_T_OBJECT)
#endif

#ifndef SYM_DATA
#define SYM_DATA(name, data...)				\
	SYM_DATA_START(name) ASM_NL				\
	data ASM_NL						\
	SYM_DATA_END(name)
#endif

#endif

#endif  
