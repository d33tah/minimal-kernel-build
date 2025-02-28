/* SPDX-License-Identifier: GPL-2.0 */

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
#if BITS_PER_LONG == 64
	return get_random_u64();
#else
	return get_random_u32();
#endif
}

/*
 * On 64-bit architectures, protect against non-terminated C string overflows
 * by zeroing out the first byte of the canary; this leaves 56 bits of entropy.
 */
# define CANARY_MASK 0xffffffffUL

static inline unsigned long get_random_canary(void)
{
	return get_random_long() & CANARY_MASK;
}

int __init random_init(const char *command_line);
bool rng_is_initialized(void);
int wait_for_random_bytes(void);

/* Calls wait_for_random_bytes() and then calls get_random_bytes(buf, nbytes).
 * Returns the result of the call to wait_for_random_bytes. */
static inline int get_random_bytes_wait(void *buf, size_t nbytes)
{
	int ret = wait_for_random_bytes();
	get_random_bytes(buf, nbytes);
	return ret;
}

#define declare_get_random_var_wait(name, ret_type) \
	static inline int get_random_ ## name ## _wait(ret_type *out) { \
		int ret = wait_for_random_bytes(); \
		if (unlikely(ret)) \
			return ret; \
		*out = get_random_ ## name(); \
		return 0; \
	}
declare_get_random_var_wait(u32, u32)
declare_get_random_var_wait(u64, u32)
declare_get_random_var_wait(int, unsigned int)
declare_get_random_var_wait(long, unsigned long)
#undef declare_get_random_var

/*
 * This is designed to be standalone for just prandom
 * users, but for now we include it from <linux/random.h>
 * for legacy reasons.
 */
#include <linux/prandom.h>

static inline bool __must_check arch_get_random_long(unsigned long *v) { return false; }
static inline bool __must_check arch_get_random_int(unsigned int *v) { return false; }
static inline bool __must_check arch_get_random_seed_long(unsigned long *v) { return false; }
static inline bool __must_check arch_get_random_seed_int(unsigned int *v) { return false; }

/*
 * Called from the boot CPU during startup; not valid to call once
 * secondary CPUs are up and preemption is possible.
 */
#ifndef arch_get_random_seed_long_early
static inline bool __init arch_get_random_seed_long_early(unsigned long *v)
{
	WARN_ON(system_state != SYSTEM_BOOTING);
	return arch_get_random_seed_long(v);
}
#endif

#ifndef arch_get_random_long_early
static inline bool __init arch_get_random_long_early(unsigned long *v)
{
	WARN_ON(system_state != SYSTEM_BOOTING);
	return arch_get_random_long(v);
}
#endif


#ifndef MODULE
extern const struct file_operations random_fops, urandom_fops;
#endif

#endif /* _LINUX_RANDOM_H */
