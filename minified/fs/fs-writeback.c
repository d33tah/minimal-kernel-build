/* Minimal includes for writeback stubs */
#include <linux/writeback.h>
#include <linux/backing-dev.h>
void inode_io_list_del(struct inode *inode)
{
}
void __mark_inode_dirty(struct inode *inode, int flags)
{
}
int write_inode_now(struct inode *inode, int sync)
{
	return 0;
}
void wb_workfn(struct work_struct *work)
{
}
int dirty_writeback_centisecs = 500;
int dirty_expire_centisecs = 3000;
void inode_wait_for_writeback(struct inode *inode)
{
}
