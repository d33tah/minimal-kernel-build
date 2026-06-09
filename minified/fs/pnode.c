/* Minimal includes for propagation stubs */
#include <linux/mount.h>
#include "pnode.h"



void change_mnt_propagation(struct mount *mnt, int type)
{
}

void propagate_mount_unlock(struct mount *mnt)
{
}

int propagate_umount(struct list_head *list)
{
	return 0;
}
