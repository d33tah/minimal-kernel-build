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

struct proc_ops {
	unsigned int proc_flags;
	int	(*proc_open)(struct inode *, struct file *);
	ssize_t	(*proc_read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*proc_read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t	(*proc_write)(struct file *, const char __user *, size_t, loff_t *);
	 
	loff_t	(*proc_lseek)(struct file *, loff_t, int);
	int	(*proc_release)(struct inode *, struct file *);
	__poll_t (*proc_poll)(struct file *, struct poll_table_struct *);
	long	(*proc_ioctl)(struct file *, unsigned int, unsigned long);
	int	(*proc_mmap)(struct file *, struct vm_area_struct *);
	unsigned long (*proc_get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
} __randomize_layout;

enum proc_hidepid {
	HIDEPID_OFF	  = 0,
	HIDEPID_NO_ACCESS = 1,
	HIDEPID_INVISIBLE = 2,
	HIDEPID_NOT_PTRACEABLE = 4,  
};

enum proc_pidonly {
	PROC_PIDONLY_OFF = 0,
	PROC_PIDONLY_ON  = 1,
};

static inline void proc_root_init(void)
{
}

static inline void proc_flush_pid(struct pid *pid)
{
}

static inline void *pde_data(const struct inode *inode) {BUG(); return NULL;}

struct ns_common;
int open_related_ns(struct ns_common *ns,
		   struct ns_common *(*get_ns)(struct ns_common *ns));

bool proc_ns_file(const struct file *file);

#endif  
