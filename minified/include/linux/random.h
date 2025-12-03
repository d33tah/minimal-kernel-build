
#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/list.h>


#include <uapi/linux/random.h>

struct notifier_block;

void add_device_randomness(const void *buf, size_t len);
void __init add_bootloader_randomness(const void *buf, size_t len);
void add_input_randomness(unsigned int type, unsigned int code,
			  unsigned int value) __latent_entropy;
void add_interrupt_randomness(int irq) __latent_entropy;
void add_hwgenerator_randomness(const void *buf, size_t len, size_t entropy);

#if defined(LATENT_ENTROPY_PLUGIN) && !defined(__CHECKER__)
static inline void add_latent_entropy(void)
{
	add_device_randomness((const void *)&latent_entropy, sizeof(latent_entropy));
}
#else
static inline void add_latent_entropy(void) { }
#endif

#if IS_ENABLED(CONFIG_VMGENID)
void add_vmfork_randomness(const void *unique_vm_id, size_t len);
int register_random_vmfork_notifier(struct notifier_block *nb);
int unregister_random_vmfork_notifier(struct notifier_block *nb);
#else
static inline int register_random_vmfork_notifier(struct notifier_block *nb) { return 0; }
static inline int unregister_random_vmfork_notifier(struct notifier_block *nb) { return 0; }
#endif

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

#include <linux/prandom.h>

static inline bool __must_check arch_get_random_long(unsigned long *v) { return false; }
static inline bool __must_check arch_get_random_int(unsigned int *v) { return false; }
static inline bool __must_check arch_get_random_seed_long(unsigned long *v) { return false; }
static inline bool __must_check arch_get_random_seed_int(unsigned int *v) { return false; }

#ifndef MODULE
extern const struct file_operations random_fops, urandom_fops;
#endif

#endif  
