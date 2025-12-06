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
#endif  
