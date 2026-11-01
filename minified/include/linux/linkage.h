/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LINKAGE_H
#define _LINUX_LINKAGE_H

#include <linux/export.h>
#include <asm/linkage.h>

#define ASM_NL ;

#define CPP_ASMLINKAGE

#define __page_aligned_bss __section(".bss..page_aligned") __aligned(PAGE_SIZE)

#define __PAGE_ALIGNED_BSS .section ".bss..page_aligned", "aw"

#define __ALIGN .align 4, 0x90

#ifdef __ASSEMBLY__

#define SYM_T_FUNC STT_FUNC

#define SYM_T_OBJECT STT_OBJECT

#define SYM_T_NONE STT_NOTYPE

/* SYM_A_* -- align the symbol? */
#define SYM_A_ALIGN ALIGN
#define SYM_A_NONE /* nothing */

/* SYM_L_* -- linkage of symbols */
#define SYM_L_GLOBAL(name) .globl name
#define SYM_L_LOCAL(name) /* nothing */

#ifndef LINKER_SCRIPT
#define ALIGN __ALIGN

#endif

/* clang-format off */
#define SYM_ENTRY(name, linkage, align...)		\
	linkage(name) ASM_NL				\
	align ASM_NL					\
	name:
/* clang-format on */

#define SYM_START(name, linkage, align...) SYM_ENTRY(name, linkage, align)

/* clang-format off */
#define SYM_END(name, sym_type)				\
	.type name sym_type ASM_NL			\
	.set .L__sym_size_##name, .-name ASM_NL		\
	.size name, .L__sym_size_##name
/* clang-format on */

#define SYM_FUNC_END(name) SYM_END(name, SYM_T_FUNC)

#define SYM_CODE_START(name) SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)

#define SYM_CODE_START_LOCAL(name) SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)

#define SYM_CODE_START_LOCAL_NOALIGN(name) \
	SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)

#define SYM_CODE_END(name) SYM_END(name, SYM_T_NONE)

#define SYM_DATA_START(name) SYM_START(name, SYM_L_GLOBAL, SYM_A_NONE)

#define SYM_DATA_START_LOCAL(name) SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)

#define SYM_DATA_END(name) SYM_END(name, SYM_T_OBJECT)

/* clang-format off */
#define SYM_DATA_END_LABEL(name, linkage, label)	\
	linkage(label) ASM_NL				\
	.type label SYM_T_OBJECT ASM_NL			\
	label:						\
	SYM_END(name, SYM_T_OBJECT)
/* clang-format on */

/* clang-format off */
#define SYM_DATA(name, data...)				\
	SYM_DATA_START(name) ASM_NL				\
	data ASM_NL						\
	SYM_DATA_END(name)
/* clang-format on */

#endif

#endif
