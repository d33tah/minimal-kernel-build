
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/ns_common.h>

struct vfsmount;

struct mnt_namespace {
	struct ns_common	ns;
	struct mount *	root;
	 
	struct list_head	list;
	struct user_namespace	*user_ns;
	struct ucounts		*ucounts;
	u64			seq;	 
	wait_queue_head_t poll;
	u64 event;
	unsigned int		mounts;  
	unsigned int		pending_mounts;
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
	int mnt_expiry_mark;
} __randomize_layout;

#define MNT_NS_INTERNAL ERR_PTR(-EINVAL)  

static inline struct mount *real_mount(struct vfsmount *mnt)
{
	return container_of(mnt, struct mount, mnt);
}

extern int __legitimize_mnt(struct vfsmount *, unsigned);
extern bool legitimize_mnt(struct vfsmount *, unsigned);

static inline void get_mnt_ns(struct mnt_namespace *ns)
{
	refcount_inc(&ns->ns.count);
}

extern seqlock_t mount_lock;

static inline bool is_anon_ns(struct mnt_namespace *ns)
{
	return ns->seq == 0;
}
