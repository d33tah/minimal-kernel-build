/* Minimal includes for splice stubs */
#include <linux/syscalls.h>
#include <linux/splice.h>
#include <linux/pipe_fs_i.h>


ssize_t generic_file_splice_read(struct file *in, loff_t *ppos,
				 struct pipe_inode_info *pipe, size_t len,
				 unsigned int flags) { return 0; }

ssize_t iter_file_splice_write(struct pipe_inode_info *pipe, struct file *out,
			       loff_t *ppos, size_t len, unsigned int flags) { return 0; }


ssize_t splice_from_pipe(struct pipe_inode_info *pipe, struct file *out,
			 loff_t *ppos, size_t len, unsigned int flags,
			 splice_actor *actor) { return 0; }
