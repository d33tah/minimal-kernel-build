/* Minimal includes for writeback stubs */
#include <linux/writeback.h>
#include <linux/backing-dev.h>

/* inode_io_list_del, inode_wait_for_writeback removed - empty stubs */
/* __mark_inode_dirty removed - inlined as empty into fs.h */
/* write_inode_now removed - returns 0 stub, caller simplified */
/* dirty_writeback_centisecs, dirty_expire_centisecs removed - never used */
void wb_workfn(struct work_struct *work)
{
}
