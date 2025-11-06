// SPDX-License-Identifier: GPL-2.0
/* Stubbed read_write.c */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>

const struct file_operations generic_ro_fops = {
	.llseek = generic_file_llseek,
	.read_iter = generic_file_read_iter,
};
EXPORT_SYMBOL(generic_ro_fops);

loff_t vfs_setpos(struct file *file, loff_t offset, loff_t maxsize) { return offset; }
EXPORT_SYMBOL(vfs_setpos);

loff_t generic_file_llseek_size(struct file *file, loff_t offset, int whence,
				 loff_t maxsize, loff_t eof) { return 0; }
EXPORT_SYMBOL(generic_file_llseek_size);

loff_t generic_file_llseek(struct file *file, loff_t offset, int whence) { return 0; }
EXPORT_SYMBOL(generic_file_llseek);

loff_t fixed_size_llseek(struct file *file, loff_t offset, int whence, loff_t size) { return 0; }
EXPORT_SYMBOL(fixed_size_llseek);

loff_t no_llseek(struct file *file, loff_t offset, int whence) { return -ESPIPE; }
EXPORT_SYMBOL(no_llseek);

loff_t noop_llseek(struct file *file, loff_t offset, int whence) { return file->f_pos; }
EXPORT_SYMBOL(noop_llseek);

loff_t default_llseek(struct file *file, loff_t offset, int whence) { return 0; }
EXPORT_SYMBOL(default_llseek);

loff_t vfs_llseek(struct file *file, loff_t offset, int whence) { return 0; }
EXPORT_SYMBOL(vfs_llseek);

SYSCALL_DEFINE3(lseek, unsigned int, fd, off_t, offset, unsigned int, whence) { return -ENOSYS; }
SYSCALL_DEFINE5(llseek, unsigned int, fd, unsigned long, offset_high,
		 unsigned long, offset_low, loff_t __user *, result,
		 unsigned int, whence) { return -ENOSYS; }

ssize_t vfs_iter_read(struct file *file, struct iov_iter *iter, loff_t *ppos,
		      rwf_t flags) { return 0; }
EXPORT_SYMBOL(vfs_iter_read);

ssize_t vfs_iter_write(struct file *file, struct iov_iter *iter, loff_t *ppos,
		       rwf_t flags) { return 0; }
EXPORT_SYMBOL(vfs_iter_write);

ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos) { return 0; }
EXPORT_SYMBOL(vfs_read);

ssize_t vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos) { return 0; }
EXPORT_SYMBOL(vfs_write);

ssize_t kernel_read(struct file *file, void *buf, size_t count, loff_t *pos) { return 0; }
EXPORT_SYMBOL(kernel_read);

ssize_t kernel_write(struct file *file, const void *buf, size_t count, loff_t *pos) { return 0; }
EXPORT_SYMBOL(kernel_write);

SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count) { return -ENOSYS; }

SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf, size_t, count) { return -ENOSYS; }

SYSCALL_DEFINE4(pread64, unsigned int, fd, char __user *, buf,
		size_t, count, loff_t, pos) { return -ENOSYS; }

SYSCALL_DEFINE4(pwrite64, unsigned int, fd, const char __user *, buf,
		 size_t, count, loff_t, pos) { return -ENOSYS; }

ssize_t vfs_readv(struct file *file, const struct iovec __user *vec,
		  unsigned long vlen, loff_t *pos, rwf_t flags) { return 0; }

ssize_t vfs_writev(struct file *file, const struct iovec __user *vec,
		   unsigned long vlen, loff_t *pos, rwf_t flags) { return 0; }

SYSCALL_DEFINE3(readv, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen) { return -ENOSYS; }

SYSCALL_DEFINE3(writev, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen) { return -ENOSYS; }

SYSCALL_DEFINE5(preadv, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h) { return -ENOSYS; }

SYSCALL_DEFINE5(pwritev, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h) { return -ENOSYS; }

SYSCALL_DEFINE6(preadv2, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h,
		rwf_t, flags) { return -ENOSYS; }

SYSCALL_DEFINE6(pwritev2, unsigned long, fd, const struct iovec __user *, vec,
		 unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h,
		 rwf_t, flags) { return -ENOSYS; }

ssize_t ksys_pread64(unsigned int fd, char __user *buf, size_t count, loff_t pos) { return -ENOSYS; }
ssize_t ksys_pwrite64(unsigned int fd, const char __user *buf, size_t count, loff_t pos) { return -ENOSYS; }
ssize_t generic_write_checks(struct kiocb *iocb, struct iov_iter *from) { return 0; }

ssize_t do_sendfile(int out_fd, int in_fd, loff_t *ppos, size_t count,
		    loff_t max) { return 0; }

SYSCALL_DEFINE4(sendfile, int, out_fd, int, in_fd, off_t __user *, offset,
		size_t, count) { return -ENOSYS; }

SYSCALL_DEFINE4(sendfile64, int, out_fd, int, in_fd, loff_t __user *, offset,
		size_t, count) { return -ENOSYS; }
