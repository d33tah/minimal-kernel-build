/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The Kernel Concurrency Sanitizer (KCSAN) infrastructure. Public interface and
 * data structures to set up runtime. See kcsan-checks.h for explicit checks and
 * modifiers. For more info please see Documentation/dev-tools/kcsan.rst.
 *
 * Copyright (C) 2019, Google LLC.
 */

#ifndef _LINUX_KCSAN_H
#define _LINUX_KCSAN_H

#include <linux/kcsan-checks.h>
#include <linux/types.h>


static inline void kcsan_init(void)			{ }


#endif /* _LINUX_KCSAN_H */
