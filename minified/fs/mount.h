
#include <linux/mount.h>
#include <linux/compiler.h>
#include <linux/ktime.h>
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
	wait_queue_head_t poll;
	u64 event;
	unsigned int		mounts;  
	unsigned int		pending_mounts;
} __randomize_layout;

struct mountpoint {
	struct hlist_node m_hash;
	struct dentry *m_dentry;
	struct hlist_head m_list;
	int m_count;
};

struct mount {
	struct hlist_node mnt_hash;
	struct mount *mnt_parent;
	struct dentry *mnt_mountpoint;
	struct vfsmount mnt;
	union {
		struct rcu_head mnt_rcu;
		struct llist_node mnt_llist;
	};
	int mnt_count;
	int mnt_writers;
	struct list_head mnt_mounts;	 
	struct list_head mnt_child;	 
	struct list_head mnt_instance;	 
	const char *mnt_devname;	 
	struct list_head mnt_list;
	struct list_head mnt_expire;	 
	struct list_head mnt_share;	 
	struct list_head mnt_slave_list; 
	struct list_head mnt_slave;	 
	struct mount *mnt_master;	 
	struct mnt_namespace *mnt_ns;	 
	struct mountpoint *mnt_mp;	 
	union {
		struct hlist_node mnt_mp_list;	 
		struct hlist_node mnt_umount;
	};
	struct list_head mnt_umounting;  
	int mnt_id;			 
	int mnt_group_id;		 
	int mnt_expiry_mark;		 
	struct hlist_head mnt_pins;
	struct hlist_head mnt_stuck_children;
} __randomize_layout;

#define MNT_NS_INTERNAL ERR_PTR(-EINVAL)  

static inline struct mount *real_mount(struct vfsmount *mnt)
{
	return container_of(mnt, struct mount, mnt);
}

extern seqlock_t mount_lock;

static inline bool is_anon_ns(struct mnt_namespace *ns)
{
	return ns->seq == 0;
}
