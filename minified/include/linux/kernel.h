#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <linux/stdarg.h>
#include <linux/limits.h>
#include <linux/const.h>

/* Inlined from align.h */
#define ALIGN(x, a)		__ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a)	__ALIGN_KERNEL((x) - ((a) - 1), (a))
#define __ALIGN_MASK(x, mask)	__ALIGN_KERNEL_MASK((x), (mask))
#define PTR_ALIGN(p, a)		((typeof(p))ALIGN((unsigned long)(p), (a)))
#define PTR_ALIGN_DOWN(p, a)	((typeof(p))ALIGN_DOWN((unsigned long)(p), (a)))
#define IS_ALIGNED(x, a)		(((x) & ((typeof(x))(a) - 1)) == 0)
#include <linux/linkage.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/container_of.h>
#include <linux/bitops.h>
#include <linux/kstrtox.h>
#include <linux/log2.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/typecheck.h>
#include <linux/panic.h>
#include <linux/printk.h>
#include <linux/build_bug.h>
#include <linux/static_call_types.h>
/* instruction_pointer.h inlined */
#define _RET_IP_		(unsigned long)__builtin_return_address(0)
#define _THIS_IP_  ({ __label__ __here; __here: (unsigned long)&&__here; })
#include <asm/byteorder.h>

/* From uapi/linux/sysinfo.h - inlined */
#define SI_LOAD_SHIFT	16
struct sysinfo {
	__kernel_long_t uptime;
	__kernel_ulong_t loads[3];
	__kernel_ulong_t totalram;
	__kernel_ulong_t freeram;
	__kernel_ulong_t sharedram;
	__kernel_ulong_t bufferram;
	__kernel_ulong_t totalswap;
	__kernel_ulong_t freeswap;
	__u16 procs;
	__u16 pad;
	__kernel_ulong_t totalhigh;
	__kernel_ulong_t freehigh;
	__u32 mem_unit;
	char _f[20-2*sizeof(__kernel_ulong_t)-sizeof(__u32)];
};

#include <linux/const.h>

#define STACK_MAGIC	0xdeadbeef

#define REPEAT_BYTE(x)	((~0ul / 0xff) * (x))

#define READ			0
#define WRITE			1

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

#define PTR_IF(cond, ptr)	((cond) ? (ptr) : NULL)

#define u64_to_user_ptr(x) (		\
{					\
	typecheck(u64, (x));		\
	(void __user *)(uintptr_t)(x);	\
}					\
)

#define upper_32_bits(n) ((u32)(((n) >> 16) >> 16))

#define lower_32_bits(n) ((u32)((n) & 0xffffffff))

#define upper_16_bits(n) ((u16)((n) >> 16))

#define lower_16_bits(n) ((u16)((n) & 0xffff))

struct completion;
struct user;


# define might_resched() do { } while (0)


  static inline void __might_resched(const char *file, int line,
				     unsigned int offsets) { }
# define might_sleep() do { might_resched(); } while (0)
# define cant_sleep() do { } while (0)
# define cant_migrate()		do { } while (0)
# define sched_annotate_sleep() do { } while (0)
# define non_block_start() do { } while (0)
# define non_block_end() do { } while (0)

#define might_sleep_if(cond) do { if (cond) might_sleep(); } while (0)

#if defined(CONFIG_MMU) && \
	(defined(CONFIG_PROVE_LOCKING) || defined(CONFIG_DEBUG_ATOMIC_SLEEP))
#define might_fault() __might_fault(__FILE__, __LINE__)
void __might_fault(const char *file, int line);
#else
static inline void might_fault(void) { }
#endif

void do_exit(long error_code) __noreturn;

extern __printf(2, 3) int sprintf(char *buf, const char * fmt, ...);
extern __printf(2, 0) int vsprintf(char *buf, const char *, va_list);
extern __printf(3, 4)
int snprintf(char *buf, size_t size, const char *fmt, ...);
extern __printf(3, 0)
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
extern __printf(3, 4)
int scnprintf(char *buf, size_t size, const char *fmt, ...);
extern __printf(3, 0)
int vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
extern __printf(2, 3) __malloc
char *kasprintf(gfp_t gfp, const char *fmt, ...);
extern __printf(2, 0) __malloc
char *kvasprintf(gfp_t gfp, const char *fmt, va_list args);
extern __printf(2, 0)
const char *kvasprintf_const(gfp_t gfp, const char *fmt, va_list args);

extern __scanf(2, 3)
int sscanf(const char *, const char *, ...);
extern __scanf(2, 0)
int vsscanf(const char *, const char *, va_list);

extern int no_hash_pointers_enable(char *str);

extern int get_option(char **str, int *pint);
extern char *get_options(const char *str, int nints, int *ints);
extern unsigned long long memparse(const char *ptr, char **retptr);
extern bool parse_option_str(const char *str, const char *option);
extern char *next_arg(char *args, char **param, char **val);

extern int core_kernel_text(unsigned long addr);
extern int __kernel_text_address(unsigned long addr);
extern int kernel_text_address(unsigned long addr);

extern void bust_spinlocks(int yes);

extern int root_mountflags;

extern bool early_boot_irqs_disabled;

extern enum system_states {
	SYSTEM_BOOTING,
	SYSTEM_SCHEDULING,
	SYSTEM_FREEING_INITMEM,
	SYSTEM_RUNNING,
	SYSTEM_HALT,
	SYSTEM_POWER_OFF,
	SYSTEM_RESTART,
	SYSTEM_SUSPEND,
} system_state;

extern const char hex_asc[];
#define hex_asc_lo(x)	hex_asc[((x) & 0x0f)]
#define hex_asc_hi(x)	hex_asc[((x) & 0xf0) >> 4]

static inline char *hex_byte_pack(char *buf, u8 byte)
{
	*buf++ = hex_asc_hi(byte);
	*buf++ = hex_asc_lo(byte);
	return buf;
}

extern const char hex_asc_upper[];

extern int hex_to_bin(unsigned char ch);

bool mac_pton(const char *s, u8 *mac);


/* tracing_start, tracing_stop, trace_dump_stack removed - unused */
/* tracing_on removed - unused */
static inline void tracing_off(void) { }
/* tracing_is_on, tracing_snapshot, tracing_snapshot_alloc removed - unused */
/* trace_printk, ftrace_vprintk, ftrace_dump removed - unused */

#define __COUNT_ARGS(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _n, X...) _n
#define COUNT_ARGS(X...) __COUNT_ARGS(, ##X, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

#define __CONCAT(a, b) a ## b
#define CONCATENATE(a, b) __CONCAT(a, b)


#define VERIFY_OCTAL_PERMISSIONS(perms)						\
	(BUILD_BUG_ON_ZERO((perms) < 0) +					\
	 BUILD_BUG_ON_ZERO((perms) > 0777) +					\
	  		\
	 BUILD_BUG_ON_ZERO((((perms) >> 6) & 4) < (((perms) >> 3) & 4)) +	\
	 BUILD_BUG_ON_ZERO((((perms) >> 3) & 4) < ((perms) & 4)) +		\
	  					\
	 BUILD_BUG_ON_ZERO((((perms) >> 6) & 2) < (((perms) >> 3) & 2)) +	\
	  		\
	 BUILD_BUG_ON_ZERO((perms) & 2) +					\
	 (perms))
#endif
