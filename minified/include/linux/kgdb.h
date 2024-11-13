/*
 * This provides the callbacks and functions that KGDB needs to share between
 * the core, I/O and arch-specific portions.
 *
 * Author: Amit Kale <amitkale@linsyssoft.com> and
 *         Tom Rini <trini@kernel.crashing.org>
 *
 * 2001-2004 (c) Amit S. Kale and 2003-2005 (c) MontaVista Software, Inc.
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */
#ifndef _KGDB_H_
#define _KGDB_H_

#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/atomic.h>
#include <linux/kprobes.h>
#include <asm/kgdb.h>

#define in_dbg_master() (0)
#define dbg_late_init()
static inline void kgdb_panic(const char *msg) {}
static inline void kgdb_free_init_mem(void) { }
#endif /* _KGDB_H_ */
