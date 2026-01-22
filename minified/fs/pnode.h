 
 
#ifndef _LINUX_PNODE_H
#define _LINUX_PNODE_H

#include <linux/list.h>
#include "mount.h"

#define IS_MNT_SHARED(m) ((m)->mnt.mnt_flags & MNT_SHARED)
/* IS_MNT_NEW, IS_MNT_SLAVE, CLEAR_MNT_SHARED removed - unused */
/* IS_MNT_UNBINDABLE, IS_MNT_MARKED, SET_MNT_MARK, CLEAR_MNT_MARK removed - unused */
#define IS_MNT_LOCKED(m) ((m)->mnt.mnt_flags & MNT_LOCKED)

/* CL_EXPIRE, CL_SLAVE, CL_MAKE_SHARED, CL_PRIVATE, CL_SHARED_TO_SLAVE,
   CL_COPY_UNBINDABLE, CL_COPY_MNT_NS_FILE, CL_COPY_ALL removed - only used by removed clone_mnt */

static inline void set_mnt_shared(struct mount *mnt)
{
	mnt->mnt.mnt_flags &= ~MNT_SHARED_MASK;
	mnt->mnt.mnt_flags |= MNT_SHARED;
}

/* change_mnt_propagation, propagate_mnt, propagate_umount removed - stubs that returned 0 */
/* propagate_mount_busy, get_dominating_id removed - unused */
int mnt_get_count(struct mount *mnt);
void mnt_set_mountpoint(struct mount *, struct mountpoint *,
			struct mount *);
/* mnt_change_mountpoint removed - empty stub */
/* copy_tree, is_path_reachable, count_mounts removed - unused */
#endif  
