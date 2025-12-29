/* Minimal includes for propagation stubs */
#include <linux/mount.h>
#include "pnode.h"

/* change_mnt_propagation, propagate_mount_unlock removed - empty stubs */

int propagate_mnt(struct mount *dest_mnt, struct mountpoint *dest_mp,
		  struct mount *source_mnt, struct hlist_head *tree_list)
{
	return 0;
}
int propagate_umount(struct list_head *list)
{
	return 0;
}
