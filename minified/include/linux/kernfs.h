
#ifndef __LINUX_KERNFS_H
#define __LINUX_KERNFS_H

#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/idr.h>
#include <linux/lockdep.h>
#include <linux/rbtree.h>
#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/types.h>
#include <linux/uidgid.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

struct file;
struct dentry;
struct iattr;
struct seq_file;
struct vm_area_struct;
struct vm_operations_struct;
struct super_block;
struct file_system_type;
struct poll_table_struct;
struct fs_context;

struct kernfs_fs_context;
struct kernfs_open_node;
struct kernfs_iattrs;

enum kernfs_node_type {
	KERNFS_DIR		= 0x0001,
	KERNFS_FILE		= 0x0002,
	KERNFS_LINK		= 0x0004,
};

#define KERNFS_TYPE_MASK		0x000f
#define KERNFS_FLAG_MASK		~KERNFS_TYPE_MASK
#define KERNFS_MAX_USER_XATTRS		128
#define KERNFS_USER_XATTR_SIZE_LIMIT	(128 << 10)

enum kernfs_node_flag {
	KERNFS_ACTIVATED	= 0x0010,
	KERNFS_NS		= 0x0020,
	KERNFS_HAS_SEQ_SHOW	= 0x0040,
	KERNFS_HAS_MMAP		= 0x0080,
	KERNFS_LOCKDEP		= 0x0100,
	KERNFS_SUICIDAL		= 0x0400,
	KERNFS_SUICIDED		= 0x0800,
	KERNFS_EMPTY_DIR	= 0x1000,
	KERNFS_HAS_RELEASE	= 0x2000,
};

enum kernfs_root_flag {
	 
	KERNFS_ROOT_CREATE_DEACTIVATED		= 0x0001,

	 
	KERNFS_ROOT_EXTRA_OPEN_PERM_CHECK	= 0x0002,

	 
	KERNFS_ROOT_SUPPORT_EXPORTOP		= 0x0004,

	 
	KERNFS_ROOT_SUPPORT_USER_XATTR		= 0x0008,
};

struct kernfs_elem_dir {
	unsigned long		subdirs;
	 
	struct rb_root		children;

	 
	struct kernfs_root	*root;
	 
	unsigned long		rev;
};

struct kernfs_elem_symlink {
	struct kernfs_node	*target_kn;
};

struct kernfs_elem_attr {
	const struct kernfs_ops	*ops;
	struct kernfs_open_node	*open;
	loff_t			size;
	struct kernfs_node	*notify_next;	 
};

struct kernfs_node {
	atomic_t		count;
	atomic_t		active;
	 
	struct kernfs_node	*parent;
	const char		*name;

	struct rb_node		rb;

	const void		*ns;	 
	unsigned int		hash;	 
	union {
		struct kernfs_elem_dir		dir;
		struct kernfs_elem_symlink	symlink;
		struct kernfs_elem_attr		attr;
	};

	void			*priv;

	 
	u64			id;

	unsigned short		flags;
	umode_t			mode;
	struct kernfs_iattrs	*iattr;
};

struct kernfs_syscall_ops {
	int (*show_options)(struct seq_file *sf, struct kernfs_root *root);

	int (*mkdir)(struct kernfs_node *parent, const char *name,
		     umode_t mode);
	int (*rmdir)(struct kernfs_node *kn);
	int (*rename)(struct kernfs_node *kn, struct kernfs_node *new_parent,
		      const char *new_name);
	int (*show_path)(struct seq_file *sf, struct kernfs_node *kn,
			 struct kernfs_root *root);
};

struct kernfs_open_file {
	 
	struct kernfs_node	*kn;
	struct file		*file;
	struct seq_file		*seq_file;
	void			*priv;

	 
	struct mutex		mutex;
	struct mutex		prealloc_mutex;
	int			event;
	struct list_head	list;
	char			*prealloc_buf;

	size_t			atomic_write_len;
	bool			mmapped:1;
	bool			released:1;
	const struct vm_operations_struct *vm_ops;
};

struct kernfs_ops {
	 
	int (*open)(struct kernfs_open_file *of);
	void (*release)(struct kernfs_open_file *of);

	 
	int (*seq_show)(struct seq_file *sf, void *v);

	void *(*seq_start)(struct seq_file *sf, loff_t *ppos);
	void *(*seq_next)(struct seq_file *sf, void *v, loff_t *ppos);
	void (*seq_stop)(struct seq_file *sf, void *v);

	ssize_t (*read)(struct kernfs_open_file *of, char *buf, size_t bytes,
			loff_t off);

	 
	size_t atomic_write_len;
	 
	bool prealloc;
	ssize_t (*write)(struct kernfs_open_file *of, char *buf, size_t bytes,
			 loff_t off);

	__poll_t (*poll)(struct kernfs_open_file *of,
			 struct poll_table_struct *pt);

	int (*mmap)(struct kernfs_open_file *of, struct vm_area_struct *vma);
};

struct kernfs_fs_context {
	struct kernfs_root	*root;		 
	void			*ns_tag;	 
	unsigned long		magic;		 

	 
	bool			new_sb_created;	 
};


static inline void kernfs_put(struct kernfs_node *kn) { }

static inline void kernfs_init(void) { }

#endif
