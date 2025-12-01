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

struct proc_fs_info {
	struct pid_namespace *pid_ns;
	struct dentry *proc_self;         
	struct dentry *proc_thread_self;  
	kgid_t pid_gid;
	enum proc_hidepid hide_pid;
	enum proc_pidonly pidonly;
};

static inline struct proc_fs_info *proc_sb_info(struct super_block *sb)
{
	return sb->s_fs_info;
}


static inline void proc_root_init(void)
{
}

static inline void proc_flush_pid(struct pid *pid)
{
}

static inline struct proc_dir_entry *proc_symlink(const char *name,
		struct proc_dir_entry *parent,const char *dest) { return NULL;}
static inline struct proc_dir_entry *proc_mkdir(const char *name,
	struct proc_dir_entry *parent) {return NULL;}
static inline struct proc_dir_entry *proc_create_mount_point(const char *name) { return NULL; }
static inline struct proc_dir_entry *_proc_mkdir(const char *name, umode_t mode,
		struct proc_dir_entry *parent, void *data, bool force_lookup)
{
	return NULL;
}
static inline struct proc_dir_entry *proc_mkdir_data(const char *name,
	umode_t mode, struct proc_dir_entry *parent, void *data) { return NULL; }
static inline struct proc_dir_entry *proc_mkdir_mode(const char *name,
	umode_t mode, struct proc_dir_entry *parent) { return NULL; }
#define proc_create_seq_private(name, mode, parent, ops, size, data) ({NULL;})
#define proc_create_seq_data(name, mode, parent, ops, data) ({NULL;})
#define proc_create_seq(name, mode, parent, ops) ({NULL;})
#define proc_create_single(name, mode, parent, show) ({NULL;})
#define proc_create_single_data(name, mode, parent, show, data) ({NULL;})

static inline struct proc_dir_entry *
proc_create(const char *name, umode_t mode, struct proc_dir_entry *parent,
	    const struct proc_ops *proc_ops)
{ return NULL; }

static inline struct proc_dir_entry *
proc_create_data(const char *name, umode_t mode, struct proc_dir_entry *parent,
		 const struct proc_ops *proc_ops, void *data)
{ return NULL; }

static inline void proc_set_size(struct proc_dir_entry *de, loff_t size) {}
static inline void proc_set_user(struct proc_dir_entry *de, kuid_t uid, kgid_t gid) {}
static inline void *pde_data(const struct inode *inode) {BUG(); return NULL;}
static inline void *proc_get_parent_data(const struct inode *inode) { BUG(); return NULL; }

static inline void proc_remove(struct proc_dir_entry *de) {}
#define remove_proc_entry(name, parent) do {} while (0)
static inline int remove_proc_subtree(const char *name, struct proc_dir_entry *parent) { return 0; }

#define proc_create_net_data(name, mode, parent, ops, state_size, data) ({NULL;})
#define proc_create_net(name, mode, parent, state_size, ops) ({NULL;})
#define proc_create_net_single(name, mode, parent, show, data) ({NULL;})

static inline struct pid *tgid_pidfd_to_pid(const struct file *file)
{
	return ERR_PTR(-EBADF);
}


struct net;

static inline struct proc_dir_entry *proc_net_mkdir(
	struct net *net, const char *name, struct proc_dir_entry *parent)
{
	return _proc_mkdir(name, 0, parent, net, true);
}

struct ns_common;
int open_related_ns(struct ns_common *ns,
		   struct ns_common *(*get_ns)(struct ns_common *ns));

static inline struct pid_namespace *proc_pid_ns(struct super_block *sb)
{
	return proc_sb_info(sb)->pid_ns;
}

bool proc_ns_file(const struct file *file);

#endif  
