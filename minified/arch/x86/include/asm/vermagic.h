/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_VERMAGIC_H
#define _ASM_VERMAGIC_H

#ifdef CONFIG_X86_64
/* X86_64 does not define MODULE_PROC_FAMILY */
#else
#define MODULE_PROC_FAMILY "686 "
#endif

# define MODULE_ARCH_VERMAGIC MODULE_PROC_FAMILY

#endif /* _ASM_VERMAGIC_H */
