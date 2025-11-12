// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal stub implementation of random.c for Hello World kernel
 * All functions are no-ops or return deterministic values
 */

#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

/* Stub implementations */

int wait_for_random_bytes(void)
{
	return 0;
}
EXPORT_SYMBOL(wait_for_random_bytes);

void get_random_bytes(void *buf, size_t len)
{
	memset(buf, 0, len);
}
EXPORT_SYMBOL(get_random_bytes);

u32 get_random_u32(void)
{
	return 0;
}
EXPORT_SYMBOL(get_random_u32);

u64 get_random_u64(void)
{
	return 0;
}
EXPORT_SYMBOL(get_random_u64);

int __init random_init(const char *command_line)
{
	return 0;
}

void add_device_randomness(const void *buf, size_t len)
{
}
EXPORT_SYMBOL(add_device_randomness);

void add_hwgenerator_randomness(const void *buf, size_t len, size_t entropy)
{
}
EXPORT_SYMBOL(add_hwgenerator_randomness);

void __init add_bootloader_randomness(const void *buf, size_t len)
{
}

void add_interrupt_randomness(int irq)
{
}
EXPORT_SYMBOL_GPL(add_interrupt_randomness);

void add_input_randomness(unsigned int type, unsigned int code, unsigned int value)
{
}
EXPORT_SYMBOL_GPL(add_input_randomness);

bool rng_is_initialized(void)
{
	return true;
}
EXPORT_SYMBOL(rng_is_initialized);

/* Stub file operations for /dev/random and /dev/urandom */

static ssize_t random_read(struct file *file, char __user *buf, size_t nbytes, loff_t *ppos)
{
	if (clear_user(buf, nbytes))
		return -EFAULT;
	return nbytes;
}

static ssize_t random_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	return count;
}

static __poll_t random_poll(struct file *file, poll_table *wait)
{
	return EPOLLIN | EPOLLRDNORM | EPOLLOUT | EPOLLWRNORM;
}

static long random_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

const struct file_operations random_fops = {
	.read = random_read,
	.write = random_write,
	.poll = random_poll,
	.unlocked_ioctl = random_ioctl,
	.llseek = noop_llseek,
};

const struct file_operations urandom_fops = {
	.read = random_read,
	.write = random_write,
	.unlocked_ioctl = random_ioctl,
	.llseek = noop_llseek,
};

/* Stub syscall */

SYSCALL_DEFINE3(getrandom, char __user *, buf, size_t, count, unsigned int, flags)
{
	if (clear_user(buf, count))
		return -EFAULT;
	return count;
}
