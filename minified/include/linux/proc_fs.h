#ifndef _LINUX_PROC_FS_H
#define _LINUX_PROC_FS_H
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/fs.h>
struct proc_dir_entry;
struct seq_file;
struct seq_operations;
enum proc_hidepid { HIDEPID_OFF = 0, HIDEPID_NO_ACCESS = 1, HIDEPID_INVISIBLE = 2, HIDEPID_NOT_PTRACEABLE = 4, };
enum proc_pidonly { PROC_PIDONLY_OFF = 0, PROC_PIDONLY_ON = 1, };
static inline void proc_root_init(void) {}
static inline void proc_flush_pid(struct pid *pid) {}
/* Removed uncalled: pde_data */
#endif
