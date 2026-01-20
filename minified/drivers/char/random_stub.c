/* Stub random number generator */
#include <linux/random.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

void get_random_bytes(void *buf, size_t len)
{
	memset(buf, 0, len);
}
/* get_random_u32 moved to header as inline */
/* random_init removed - empty stub */

static ssize_t random_read(struct file *file, char __user *buf, size_t nbytes,
			   loff_t *ppos)
{
	if (clear_user(buf, nbytes))
		return -EFAULT;
	return nbytes;
}
static ssize_t random_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	return count;
}
/* random_poll removed - poll/select syscalls return ENOSYS */
/* random_ioctl removed - ioctl syscall returns ENOTTY */
const struct file_operations random_fops = {
	.read = random_read,
	.write = random_write,
	/* poll removed - syscall returns ENOSYS */
	/* unlocked_ioctl removed - ioctl returns ENOTTY */
	/* llseek removed - lseek syscall returns ENOSYS */
};
const struct file_operations urandom_fops = {
	.read = random_read,
	.write = random_write,
	/* ioctl, llseek removed */
};
SYSCALL_DEFINE3(getrandom, char __user *, buf, size_t, count, unsigned int,
		flags)
{
	return count;
}
