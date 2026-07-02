#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

#include <linux/compiler.h>
#include <linux/types.h>

#if defined(__noretpoline) && !defined(MODULE)
#define __noinitretpoline __noretpoline
#else
#define __noinitretpoline
#endif


#define __init		__section(".init.text") __cold  __latent_entropy __noinitretpoline __nocfi
#define __initdata	__section(".init.data")
#define __initconst	__section(".init.rodata")

#define __ref            __section(".ref.text") noinline
#define __refdata        __section(".ref.data")
#define __exitused  __used

#define __exit          __section(".exit.text") __exitused __cold notrace

#define __meminit        __section(".meminit.text") __cold notrace \
						  __latent_entropy
#define __meminitdata    __section(".meminit.data")

#define __HEAD		.section	".head.text","ax"
#define __INIT		.section	".init.text","ax"

#define __INITDATA	.section	".init.data","aw",%progbits

#define __REFDATA        .section       ".ref.data", "aw"

#ifndef __ASSEMBLY__
typedef int (*initcall_t)(void);
typedef void (*exitcall_t)(void);

typedef int initcall_entry_t;

static inline initcall_t initcall_from_entry(initcall_entry_t *entry)
{
	return offset_to_ptr(entry);
}

extern initcall_entry_t __con_initcall_start[], __con_initcall_end[];

struct file_system_type;

extern int do_one_initcall(initcall_t fn);
extern char __initdata boot_command_line[];

void setup_arch(char **);
extern struct file_system_type rootfs_fs_type;

void mark_rodata_ro(void);

extern void (*late_time_init)(void);


#endif
  
#ifndef MODULE

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
#define postcore_initcall(fn)		__define_initcall(fn, 2)
#define subsys_initcall(fn)		__define_initcall(fn, 4)
#define fs_initcall(fn)			__define_initcall(fn, 5)
#define rootfs_initcall(fn)		__define_initcall(fn, rootfs)
#define device_initcall(fn)		__define_initcall(fn, 6)
#define late_initcall(fn)		__define_initcall(fn, 7)

#define __initcall(fn) device_initcall(fn)

#define console_initcall(fn)	___define_initcall(fn, con, .con_initcall)

/* The __setup()/early_param() registration macros + struct obs_kernel_param
 * were dropped once the last .init.setup registration went away (cmdline-parse
 * cascade, ticks #361-#371): zero __setup/early_param call-sites remain
 * tree-wide, so the .init.setup section is empty and these macros are dead.
 * The (empty) lds section markers + parse_early_param() [2 live callers] stay. */
void __init parse_early_param(void);
#endif

#endif

#endif
