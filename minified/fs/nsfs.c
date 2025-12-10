/* Minimal includes for nsfs stubs */
#include <linux/fs.h>
#include <linux/proc_ns.h>

/* --- 2025-12-06 20:38 --- nsfs.h inlined (13 LOC) */
#define NSIO	0xb7
#define NS_GET_USERNS		_IO(NSIO, 0x1)
#define NS_GET_PARENT		_IO(NSIO, 0x2)
#define NS_GET_NSTYPE		_IO(NSIO, 0x3)
#define NS_GET_OWNER_UID	_IO(NSIO, 0x4)
/* --- end nsfs.h inlined --- */

const struct dentry_operations ns_dentry_operations = {
	.d_delete	= always_delete_dentry,
};

void __init nsfs_init(void)
{
	 
}
