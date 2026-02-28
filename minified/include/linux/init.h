#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

#include <linux/compiler.h>
#include <linux/types.h>

#define __init		__section(".init.text") __cold  __latent_entropy __nocfi
#define __initdata	__section(".init.data")
#define __initconst	__section(".init.rodata")

#define __ref            __section(".ref.text") noinline
#define __refdata        __section(".ref.data")

#define __meminit        __section(".meminit.text") __cold notrace \
						  __latent_entropy
#define __meminitdata    __section(".meminit.data")

#define __HEAD		.section	".head.text","ax"
#define __INIT		.section	".init.text","ax"

#define __INITDATA	.section	".init.data","aw",%progbits

#define __REFDATA        .section       ".ref.data", "aw"

#ifndef __ASSEMBLY__
typedef int (*initcall_t)(void);

typedef int initcall_entry_t;

static inline initcall_t initcall_from_entry(initcall_entry_t *entry)
{
	return offset_to_ptr(entry);
}

struct file_system_type;

extern char __initdata boot_command_line[];

void setup_arch(char **);
void prepare_namespace(void);
extern struct file_system_type rootfs_fs_type;

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
#define core_initcall(fn)		__define_initcall(fn, 1)
#define fs_initcall(fn)			__define_initcall(fn, 5)

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

void __init parse_early_param(void);
#endif  /* __ASSEMBLY__ */

#endif  /* _LINUX_INIT_H */
