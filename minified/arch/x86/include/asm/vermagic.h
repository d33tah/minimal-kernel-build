/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_VERMAGIC_H
#define _ASM_VERMAGIC_H

#ifdef CONFIG_X86_64
/* X86_64 does not define MODULE_PROC_FAMILY */
#elif defined CONFIG_M486SX
#define MODULE_PROC_FAMILY "486SX "
#elif defined CONFIG_M486
#define MODULE_PROC_FAMILY "486 "
#elif defined CONFIG_M586
#define MODULE_PROC_FAMILY "586 "
#elif defined CONFIG_M586TSC
#define MODULE_PROC_FAMILY "586TSC "
#elif defined CONFIG_M586MMX
#define MODULE_PROC_FAMILY "586MMX "
#elif defined CONFIG_MCORE2
#define MODULE_PROC_FAMILY "CORE2 "
#elif defined CONFIG_MATOM
#define MODULE_PROC_FAMILY "ATOM "
#else
#define MODULE_PROC_FAMILY "686 "
#endif

# define MODULE_ARCH_VERMAGIC MODULE_PROC_FAMILY

#endif /* _ASM_VERMAGIC_H */
