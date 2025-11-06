// SPDX-License-Identifier: GPL-2.0-only
/* Stubbed splice.c */
#include <linux/fs.h>
#include <linux/splice.h>
#include <linux/pipe_fs_i.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include "internal.h"

ssize_t splice_to_pipe(struct pipe_inode_info *pipe, struct splice_pipe_desc *spd) { return 0; }
EXPORT_SYMBOL_GPL(splice_to_pipe);

ssize_t add_to_pipe(struct pipe_inode_info *pipe, struct pipe_buffer *buf) { return 0; }
EXPORT_SYMBOL(add_to_pipe);

ssize_t generic_file_splice_read(struct file *in, loff_t *ppos,
				 struct pipe_inode_info *pipe, size_t len,
				 unsigned int flags) { return 0; }
EXPORT_SYMBOL(generic_file_splice_read);

ssize_t __splice_from_pipe(struct pipe_inode_info *pipe, struct splice_desc *sd,
			   splice_actor *actor) { return 0; }
EXPORT_SYMBOL(__splice_from_pipe);

ssize_t iter_file_splice_write(struct pipe_inode_info *pipe, struct file *out,
			       loff_t *ppos, size_t len, unsigned int flags) { return 0; }
EXPORT_SYMBOL(iter_file_splice_write);

ssize_t generic_splice_sendpage(struct pipe_inode_info *pipe, struct file *out,
				loff_t *ppos, size_t len, unsigned int flags) { return 0; }
EXPORT_SYMBOL(generic_splice_sendpage);

ssize_t splice_direct_to_actor(struct file *in, struct splice_desc *sd,
			       splice_direct_actor *actor) { return 0; }
EXPORT_SYMBOL(splice_direct_to_actor);

long do_splice_direct(struct file *in, loff_t *ppos, struct file *out,
		      loff_t *opos, size_t len, unsigned int flags) { return 0; }
EXPORT_SYMBOL(do_splice_direct);

static void page_cache_pipe_buf_release(struct pipe_inode_info *pipe, struct pipe_buffer *buf) { }
static bool page_cache_pipe_buf_get(struct pipe_inode_info *pipe, struct pipe_buffer *buf) { return false; }
static bool page_cache_pipe_buf_try_steal(struct pipe_inode_info *pipe, struct pipe_buffer *buf) { return false; }

const struct pipe_buf_operations page_cache_pipe_buf_ops = {
	.release = page_cache_pipe_buf_release,
	.try_steal = page_cache_pipe_buf_try_steal,
	.get = page_cache_pipe_buf_get,
};

const struct pipe_buf_operations default_pipe_buf_ops = {
	.release = page_cache_pipe_buf_release,
	.try_steal = page_cache_pipe_buf_try_steal,
	.get = page_cache_pipe_buf_get,
};

ssize_t splice_from_pipe(struct pipe_inode_info *pipe, struct file *out,
			 loff_t *ppos, size_t len, unsigned int flags,
			 splice_actor *actor) { return 0; }

long splice_file_to_pipe(struct file *in, struct pipe_inode_info *opipe,
			 loff_t *offset, size_t len, unsigned int flags) { return 0; }

SYSCALL_DEFINE6(splice, int, fd_in, loff_t __user *, off_in,
		int, fd_out, loff_t __user *, off_out,
		size_t, len, unsigned int, flags) { return -ENOSYS; }
SYSCALL_DEFINE4(tee, int, fdin, int, fdout, size_t, len, unsigned int, flags) { return -ENOSYS; }
SYSCALL_DEFINE4(vmsplice, int, fd, const struct iovec __user *, iov,
		unsigned long, nr_segs, unsigned int, flags) { return -ENOSYS; }
