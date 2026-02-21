
#include <linux/mount.h>
#include <linux/compiler.h>
/* ktime.h inlined */
#include <linux/jiffies.h>
#include <asm/bug.h>
typedef s64 ktime_t;
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

typedef void (*poll_queue_proc)(struct file *, wait_queue_head_t *, struct poll_table_struct *);

struct poll_table_struct {
	poll_queue_proc _qproc;
	__poll_t _key;
};

#include <linux/ns_common.h>

struct vfsmount;

struct mnt_namespace {
	struct ns_common	ns;
	struct mount *	root;
	 
	struct list_head	list;
	spinlock_t		ns_lock;
	struct user_namespace	*user_ns;
	struct ucounts		*ucounts;
	u64			seq;
	unsigned int		mounts;
} __randomize_layout;

struct mount {
	struct vfsmount mnt;
	int mnt_count;
	int mnt_writers;
	struct list_head mnt_instance;
	const char *mnt_devname;
	struct list_head mnt_list;
	struct mnt_namespace *mnt_ns;
	int mnt_id;
} __randomize_layout;

#define MNT_NS_INTERNAL ERR_PTR(-EINVAL)  

static inline struct mount *real_mount(struct vfsmount *mnt)
{
	return container_of(mnt, struct mount, mnt);
}

extern seqlock_t mount_lock;
