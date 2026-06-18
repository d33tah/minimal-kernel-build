
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
	struct mount *mnt_parent;
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
	union {
		struct hlist_node mnt_mp_list;	 
		struct hlist_node mnt_umount;
	};
	struct list_head mnt_umounting;  
	int mnt_id;			 
	int mnt_group_id;		 
	int mnt_expiry_mark;		 
	struct hlist_head mnt_stuck_children;
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
