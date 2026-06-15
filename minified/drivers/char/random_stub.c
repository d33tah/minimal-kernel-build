/* Stub random number generator */
#include <linux/random.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>


int wait_for_random_bytes(void)
{
	return 0;
}

void get_random_bytes(void *buf, size_t len)
{
	memset(buf, 0, len);
}

u32 get_random_u32(void)
{
	return 0;
}

u64 get_random_u64(void)
{
	return 0;
}

int __init random_init(const char *command_line)
{
	return 0;
}

void add_device_randomness(const void *buf, size_t len)
{
}


void add_interrupt_randomness(int irq)
{
}


bool rng_is_initialized(void)
{
	return true;
}


SYSCALL_DEFINE3(getrandom, char __user *, buf, size_t, count, unsigned int, flags)
{
	if (clear_user(buf, count))
		return -EFAULT;
	return count;
}
