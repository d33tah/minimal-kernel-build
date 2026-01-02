 
 
#ifndef _LINUX_PNODE_H
#define _LINUX_PNODE_H

#include <linux/list.h>
#include "mount.h"

#define IS_MNT_SHARED(m) ((m)->mnt.mnt_flags & MNT_SHARED)
#define IS_MNT_SLAVE(m) ((m)->mnt_master)
#define IS_MNT_NEW(m)  (!(m)->mnt_ns || is_anon_ns((m)->mnt_ns))
#define CLEAR_MNT_SHARED(m) ((m)->mnt.mnt_flags &= ~MNT_SHARED)
#define IS_MNT_UNBINDABLE(m) ((m)->mnt.mnt_flags & MNT_UNBINDABLE)
#define IS_MNT_MARKED(m) ((m)->mnt.mnt_flags & MNT_MARKED)
#define SET_MNT_MARK(m) ((m)->mnt.mnt_flags |= MNT_MARKED)
#define CLEAR_MNT_MARK(m) ((m)->mnt.mnt_flags &= ~MNT_MARKED)
#define IS_MNT_LOCKED(m) ((m)->mnt.mnt_flags & MNT_LOCKED)

#define CL_EXPIRE    		0x01
#define CL_SLAVE     		0x02
/* CL_COPY_UNBINDABLE removed - unused */
#define CL_MAKE_SHARED 		0x08
#define CL_PRIVATE 		0x10
#define CL_SHARED_TO_SLAVE	0x20
/* CL_COPY_MNT_NS_FILE, CL_COPY_ALL removed - unused */

static inline void set_mnt_shared(struct mount *mnt)
{
	mnt->mnt.mnt_flags &= ~MNT_SHARED_MASK;
	mnt->mnt.mnt_flags |= MNT_SHARED;
}

/* change_mnt_propagation, propagate_mnt, propagate_umount removed - stubs that returned 0 */
int propagate_mount_busy(struct mount *, int);
/* get_dominating_id removed - unused */
int mnt_get_count(struct mount *mnt);
void mnt_set_mountpoint(struct mount *, struct mountpoint *,
			struct mount *);
/* mnt_change_mountpoint removed - empty stub */
struct mount *copy_tree(struct mount *, struct dentry *, int);
/* is_path_reachable, count_mounts removed - unused */
#endif  
