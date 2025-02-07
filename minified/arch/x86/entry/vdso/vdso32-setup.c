// SPDX-License-Identifier: GPL-2.0
/*
 * (C) Copyright 2002 Linus Torvalds
 * Portions based on the vdso-randomization code from exec-shield:
 * Copyright(C) 2005-2006, Red Hat, Inc., Ingo Molnar
 *
 * This file contains the needed initializations to support sysenter.
 */

#include <linux/init.h>
#include <asm/processor.h>
#include <asm/vdso.h>

#include "asm/cache.h"
#include "linux/kstrtox.h"
#include "linux/printk.h"

#define VDSO_DEFAULT	1

/*
 * Should the kernel map a VDSO page into processes and pass its
 * address down to glibc upon exec()?
 */
unsigned int __read_mostly vdso32_enabled = VDSO_DEFAULT;

static int __init vdso32_setup(char *s)
{
	vdso32_enabled = simple_strtoul(s, NULL, 0);

	if (vdso32_enabled > 1) {
		pr_warn("vdso32 values other than 0 and 1 are no longer allowed; vdso disabled\n");
		vdso32_enabled = 0;
	}

	return 1;
}

/*
 * For consistency, the argument vdso32=[012] affects the 32-bit vDSO
 * behavior on both 64-bit and 32-bit kernels.
 * On 32-bit kernels, vdso=[012] means the same thing.
 */
__setup("vdso32=", vdso32_setup);

__setup_param("vdso=", vdso_setup, vdso32_setup, 0);

int __init sysenter_setup(void)
{
	init_vdso_image(&vdso_image_32);

	return 0;
}

