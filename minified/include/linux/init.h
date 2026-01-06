#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

#include <linux/compiler.h>
#include <linux/types.h>

/* MODULE is never defined (CONFIG_MODULES=n) */
/* __noretpoline is never defined, so __noinitretpoline is always empty */
#define __noinitretpoline


#define __init		__section(".init.text") __cold  __latent_entropy __noinitretpoline __nocfi
#define __initdata	__section(".init.data")
#define __initconst	__section(".init.rodata")
/* __exitdata, __exit_call removed - never used (no modules) */

#define __ref            __section(".ref.text") noinline
#define __refdata        __section(".ref.data")

#define __exit          __section(".exit.text") __used __cold notrace

#define __meminit        __section(".meminit.text") __cold notrace \
						  __latent_entropy
#define __meminitdata    __section(".meminit.data")

#define __HEAD		.section	".head.text","ax"
#define __INIT		.section	".init.text","ax"

#define __INITDATA	.section	".init.data","aw",%progbits
/* __INITRODATA removed - never used */

/* __REF removed - never used */
#define __REFDATA        .section       ".ref.data", "aw"

#ifndef __ASSEMBLY__
typedef int (*initcall_t)(void);
/* exitcall_t removed - never used (no modules) */

typedef int initcall_entry_t;

static inline initcall_t initcall_from_entry(initcall_entry_t *entry)
{
	return offset_to_ptr(entry);
}

extern initcall_entry_t __con_initcall_start[], __con_initcall_end[];

/* typedef ctor_fn_t removed - never used */

struct file_system_type;

extern int do_one_initcall(initcall_t fn);
extern char __initdata boot_command_line[];

void setup_arch(char **);
void prepare_namespace(void);
void __init init_rootfs(void);
extern struct file_system_type rootfs_fs_type;

void mark_rodata_ro(void);

extern void (*late_time_init)(void);


#endif

#ifndef __ASSEMBLY__


#define __initcall_id(fn)					\
	__PASTE(__KBUILD_MODNAME,				\
	__PASTE(__,						\
	__PASTE(__COUNTER__,					\
	__PASTE(_,						\
	__PASTE(__LINE__,					\
	__PASTE(_, fn))))))

#define __initcall_name(prefix, __iid, id)			\
	__PASTE(__,						\
	__PASTE(prefix,						\
	__PASTE(__,						\
	__PASTE(__iid, id))))

#define __initcall_section(__sec, __iid)			\
	#__sec ".init"

#define __initcall_stub(fn, __iid, id)	fn

#define __define_initcall_stub(__stub, fn)			\
	__ADDRESSABLE(fn)

#define ____define_initcall(fn, __stub, __name, __sec)		\
	__define_initcall_stub(__stub, fn)			\
	asm(".section	\"" __sec "\", \"a\"		\n"	\
	    __stringify(__name) ":			\n"	\
	    ".long	" __stringify(__stub) " - .	\n"	\
	    ".previous					\n");	\
	static_assert(__same_type(initcall_t, &fn));

#define __unique_initcall(fn, id, __sec, __iid)			\
	____define_initcall(fn,					\
		__initcall_stub(fn, __iid, id),			\
		__initcall_name(initcall, __iid, id),		\
		__initcall_section(__sec, __iid))

#define ___define_initcall(fn, id, __sec)			\
	__unique_initcall(fn, id, __sec, __initcall_id(fn))

#define __define_initcall(fn, id) ___define_initcall(fn, id, .initcall##id)

#define early_initcall(fn)		__define_initcall(fn, early)
/* pure_initcall removed - unused */
#define core_initcall(fn)		__define_initcall(fn, 1)
/* core_initcall_sync removed - unused */
#define postcore_initcall(fn)		__define_initcall(fn, 2)
/* postcore_initcall_sync, arch_initcall, arch_initcall_sync removed - unused */
#define subsys_initcall(fn)		__define_initcall(fn, 4)
/* subsys_initcall_sync removed - unused */
#define fs_initcall(fn)			__define_initcall(fn, 5)
/* fs_initcall_sync removed - unused */
#define rootfs_initcall(fn)		__define_initcall(fn, rootfs)
#define device_initcall(fn)		__define_initcall(fn, 6)
/* device_initcall_sync removed - unused */
#define late_initcall(fn)		__define_initcall(fn, 7)
/* late_initcall_sync removed - unused */

#define __initcall(fn) device_initcall(fn)

/* __exitcall removed - never used (no modules) */

#define console_initcall(fn)	___define_initcall(fn, con, .con_initcall)

struct obs_kernel_param {
	const char *str;
	int (*setup_func)(char *);
	int early;
};

#define __setup_param(str, unique_id, fn, early)			\
	static const char __setup_str_##unique_id[] __initconst		\
		__aligned(1) = str; 					\
	static struct obs_kernel_param __setup_##unique_id		\
		__used __section(".init.setup")				\
		__aligned(__alignof__(struct obs_kernel_param))		\
		= { __setup_str_##unique_id, fn, early }

#define __setup(str, fn)						\
	__setup_param(str, fn, fn, 0)

#define early_param(str, fn)						\
	__setup_param(str, fn, fn, 1)

#define early_param_on_off(str_on, str_off, var, config)		\
									\
	int var = IS_ENABLED(config);					\
									\
	static int __init parse_##var##_on(char *arg)			\
	{								\
		var = 1;						\
		return 0;						\
	}								\
	early_param(str_on, parse_##var##_on);				\
									\
	static int __init parse_##var##_off(char *arg)			\
	{								\
		var = 0;						\
		return 0;						\
	}								\
	early_param(str_off, parse_##var##_off)

void __init parse_early_param(void);
void __init parse_early_options(char *cmdline);
#endif  /* __ASSEMBLY__ */

/* __nosavedata, __exit_p removed - never used */

#endif  /* _LINUX_INIT_H */
