/* Minimal includes for splice stubs */
#include <linux/syscalls.h>
#include <linux/splice.h>
#include <linux/pipe_fs_i.h>
/* generic_file_splice_read, iter_file_splice_write removed - never called */
static void pipe_buf_stub_release(struct pipe_inode_info *p,
				  struct pipe_buffer *b)
{
}
/* pipe_buf_stub_get, pipe_buf_stub_steal removed - callbacks never invoked */
static const struct pipe_buf_operations stub_pipe_buf_ops = {
	.release = pipe_buf_stub_release,
};
const struct pipe_buf_operations page_cache_pipe_buf_ops = stub_pipe_buf_ops;
const struct pipe_buf_operations default_pipe_buf_ops = stub_pipe_buf_ops;
