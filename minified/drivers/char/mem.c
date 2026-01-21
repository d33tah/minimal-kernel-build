
#include <linux/mm.h>
#include <linux/slab.h>

#include <linux/major.h>
/* MISC_DYNAMIC_MINOR, struct miscdevice removed - unused */
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/shmem_fs.h>
#include <linux/splice.h>
#include <linux/pfn.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/uio.h>
#include <linux/uaccess.h>
#include <linux/security.h>

/* DEVMEM_MINOR, DEVPORT_MINOR removed - unused */

static ssize_t read_null(struct file *file, char __user *buf, size_t count,
			 loff_t *ppos)
{
	return 0;
}

static ssize_t write_null(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	return count;
}

static ssize_t read_iter_null(struct kiocb *iocb, struct iov_iter *to)
{
	return 0;
}

static ssize_t write_iter_null(struct kiocb *iocb, struct iov_iter *from)
{
	size_t count = iov_iter_count(from);
	iov_iter_advance(from, count);
	return count;
}

/* pipe_to_null, splice_write_null removed - splice syscall returns ENOSYS */

static ssize_t read_iter_zero(struct kiocb *iocb, struct iov_iter *iter)
{
	size_t written = 0;

	while (iov_iter_count(iter)) {
		size_t chunk = iov_iter_count(iter), n;

		if (chunk > PAGE_SIZE)
			chunk = PAGE_SIZE;
		n = iov_iter_zero(chunk, iter);
		if (!n && iov_iter_count(iter))
			return written ? written : -EFAULT;
		written += n;
		if (signal_pending(current))
			return written ? written : -ERESTARTSYS;
		if (!need_resched())
			continue;
		if (iocb->ki_flags & IOCB_NOWAIT)
			return written ? written : -EAGAIN;
		cond_resched();
	}
	return written;
}

static ssize_t read_zero(struct file *file, char __user *buf, size_t count,
			 loff_t *ppos)
{
	size_t cleared = 0;

	while (count) {
		size_t chunk = min_t(size_t, count, PAGE_SIZE);
		size_t left;

		left = clear_user(buf + cleared, chunk);
		if (unlikely(left)) {
			cleared += (chunk - left);
			if (!cleared)
				return -EFAULT;
			break;
		}
		cleared += chunk;
		count -= chunk;

		if (signal_pending(current))
			break;
		cond_resched();
	}

	return cleared;
}

static int mmap_zero(struct file *file, struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_SHARED)
		return shmem_zero_setup(vma);
	vma_set_anonymous(vma);
	return 0;
}

static unsigned long
get_unmapped_area_zero(struct file *file, unsigned long addr, unsigned long len,
		       unsigned long pgoff, unsigned long flags)
{
	if (flags & MAP_SHARED) {
		return shmem_get_unmapped_area(NULL, addr, len, pgoff, flags);
	}

	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

static ssize_t write_full(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	return -ENOSPC;
}

/* null_lseek removed - never used */

#define write_zero write_null
#define write_iter_zero write_iter_null

static const struct file_operations null_fops = {
	/* llseek removed - lseek syscall returns ENOSYS */
	.read = read_null,
	.write = write_null,
	.read_iter = read_iter_null,
	.write_iter = write_iter_null,
	/* splice_write removed - splice syscall returns ENOSYS */
};

static const struct file_operations zero_fops = {
	/* llseek removed - lseek syscall returns ENOSYS */
	.write = write_zero, .read_iter = read_iter_zero,
	.read = read_zero,   .write_iter = write_iter_zero,
	.mmap = mmap_zero,   .get_unmapped_area = get_unmapped_area_zero,
};

static const struct file_operations full_fops = {
	/* llseek removed - lseek syscall returns ENOSYS */
	.read_iter = read_iter_zero,
	.write = write_full,
};

static const struct memdev {
	const char *name;
	umode_t mode;
	const struct file_operations *fops;
	fmode_t fmode;
} devlist[] = {
	[3] = { "null", 0666, &null_fops, FMODE_NOWAIT },
	[5] = { "zero", 0666, &zero_fops, FMODE_NOWAIT },
	[7] = { "full", 0666, &full_fops, 0 },
	[8] = { "random", 0666, &random_fops, 0 },
	[9] = { "urandom", 0666, &urandom_fops, 0 },
};

/* memory_open, memory_fops removed - never called */

/* mem_devnode, mem_class removed - unused */

/* chr_dev_init removed - tty_init hangs with low memory */
/* Hello World uses direct VGA writes, doesn't need TTY */
