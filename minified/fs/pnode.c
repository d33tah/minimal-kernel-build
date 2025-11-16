 
#include <linux/mnt_namespace.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include <linux/nsproxy.h>
#include <uapi/linux/mount.h>
#include "internal.h"
#include "pnode.h"

 

int get_dominating_id(struct mount *mnt, const struct path *root)
{
	return 0;
}

void change_mnt_propagation(struct mount *mnt, int type)
{
}

int propagate_mnt(struct mount *dest_mnt, struct mountpoint *dest_mp,
		    struct mount *source_mnt, struct hlist_head *tree_list)
{
	return 0;
}

int propagate_mount_busy(struct mount *mnt, int refcnt)
{
	return 0;
}

void propagate_mount_unlock(struct mount *mnt)
{
}

int propagate_umount(struct list_head *list)
{
	return 0;
}
