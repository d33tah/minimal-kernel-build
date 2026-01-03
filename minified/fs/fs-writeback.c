/* Minimal includes for writeback stubs */
#include <linux/writeback.h>
#include <linux/backing-dev.h>

/* inode_io_list_del, inode_wait_for_writeback removed - empty stubs */
/* __mark_inode_dirty removed - inlined as empty into fs.h */
int write_inode_now(struct inode *inode, int sync)
{
	return 0;
}
void wb_workfn(struct work_struct *work)
{
}
int dirty_writeback_centisecs = 500;
int dirty_expire_centisecs = 3000;
