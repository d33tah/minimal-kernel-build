#ifndef _LINUX_PROC_FS_H
#define _LINUX_PROC_FS_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/fs.h>

struct proc_dir_entry;
struct seq_file;
struct seq_operations;

enum {
	 
#ifdef MODULE
	PROC_ENTRY_PERMANENT = 0U,
#else
	PROC_ENTRY_PERMANENT = 1U << 0,
#endif
};

static inline void proc_root_init(void)
{
}

static inline void proc_flush_pid(struct pid *pid)
{
}


#endif
