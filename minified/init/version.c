// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/init/version.c
 *
 *  Copyright (C) 1992  Theodore Ts'o
 *
 *  May be freely distributed as part of Linux.
 */

#include <generated/compile.h>
#include <linux/utsname.h>

/* Only .machine is read (AT_PLATFORM, check_bugs); there is no uname(2). */
struct uts_namespace init_uts_ns = {
	.name = { .machine = UTS_MACHINE },
};
