/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_HPET_H
#define _ASM_X86_HPET_H

#include <linux/msi.h>


static inline int hpet_enable(void) { return 0; }
static inline int is_hpet_enabled(void) { return 0; }
#define hpet_readl(a) 0
#define default_setup_hpet_msi	NULL

#endif /* _ASM_X86_HPET_H */
