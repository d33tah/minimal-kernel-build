/* Minimal includes for propagation stubs */
#include <linux/mount.h>
#include "pnode.h"
void change_mnt_propagation(struct mount *mnt, int type)
{
}
int propagate_mnt(struct mount *dest_mnt, struct mountpoint *dest_mp,
		  struct mount *source_mnt, struct hlist_head *tree_list)
{
	return 0;
}
/* propagate_mount_busy removed - never called */
void propagate_mount_unlock(struct mount *mnt)
{
}
int propagate_umount(struct list_head *list)
{
	return 0;
}
