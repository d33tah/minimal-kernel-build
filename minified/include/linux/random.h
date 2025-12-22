
#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/irqnr.h>

struct notifier_block;

void add_device_randomness(const void *buf, size_t len);
void add_interrupt_randomness(int irq) __latent_entropy;

static inline void add_latent_entropy(void) { }


void get_random_bytes(void *buf, size_t len);
u32 get_random_u32(void);
u64 get_random_u64(void);
static inline unsigned int get_random_int(void)
{
	return get_random_u32();
}
static inline unsigned long get_random_long(void)
{
	return get_random_u32(); /* BITS_PER_LONG == 32 */
}

# define CANARY_MASK 0xffffffffUL

int __init random_init(const char *command_line);
bool rng_is_initialized(void);
int wait_for_random_bytes(void);

#ifndef MODULE
extern const struct file_operations random_fops, urandom_fops;
#endif

#endif  
