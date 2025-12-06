
#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/irqnr.h>

/* From uapi/linux/random.h - inlined */
#define RNDGETENTCNT	_IOR( 'R', 0x00, int )
#define RNDADDTOENTCNT	_IOW( 'R', 0x01, int )
#define RNDGETPOOL	_IOR( 'R', 0x02, int [2] )
#define RNDADDENTROPY	_IOW( 'R', 0x03, int [2] )
#define RNDZAPENTCNT	_IO( 'R', 0x04 )
#define RNDCLEARPOOL	_IO( 'R', 0x06 )
#define RNDRESEEDCRNG	_IO( 'R', 0x07 )

struct rand_pool_info {
	int	entropy_count;
	int	buf_size;
	__u32	buf[0];
};

#define GRND_NONBLOCK	0x0001
#define GRND_RANDOM	0x0002
#define GRND_INSECURE	0x0004

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
