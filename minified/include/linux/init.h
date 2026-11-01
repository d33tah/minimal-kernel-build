/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

#include <linux/compiler.h>

#define __noinitretpoline

#define __init                  \
	__section(".init.text") \
		__cold __latent_entropy __noinitretpoline __nocfi
#define __initdata __section(".init.data")
#define __initconst __section(".init.rodata")

/*
 * modpost check for section mismatches during the kernel build.
 * A section mismatch happens when there are references from a
 * code or data section to an init section (both code or data).
 * The init sections are (for most archs) discarded by the kernel
 * when early init has completed so all such references are potential bugs.
 * For exit sections the same issue exists.
 *
 * The following markers are used for the cases where the reference to
 * the *init / *exit section (code or data) is valid and will teach
 * modpost not to issue a warning.  Intended semantics is that a code or
 * data tagged __ref* can reference code or data from init section without
 * producing a warning (of course, no warning does not mean code is
 * correct, so optimally document why the __ref is needed and why it's OK).
 *
 * The markers follow same syntax rules as __init / __initdata.
 */
#define __ref __section(".ref.text") noinline
#define __refdata __section(".ref.data")

#define __meminit __section(".meminit.text") __cold notrace __latent_entropy
#define __meminitdata __section(".meminit.data")

/* For assembly routines */
#define __HEAD .section ".head.text", "ax"
#define __INIT .section ".init.text", "ax"

#define __INITDATA .section ".init.data", "aw", % progbits

#ifndef __ASSEMBLY__
/*
 * Used for initialization calls..
 */
typedef int (*initcall_t)(void);

typedef int initcall_entry_t;

static inline initcall_t initcall_from_entry(initcall_entry_t *entry)
{
	return offset_to_ptr(entry);
}

extern initcall_entry_t __con_initcall_start[], __con_initcall_end[];

void setup_arch(void);
extern struct file_system_type rootfs_fs_type;

#endif

#ifndef __ASSEMBLY__

/*
 * initcalls are now grouped by functionality into separate
 * subsections. Ordering inside the subsections is determined
 * by link order. 
 * For backwards compatibility, initcall() puts the call in 
 * the device init subsection.
 *
 * The `id' arg to __define_initcall() is needed so that multiple initcalls
 * can point at the same handler without causing duplicate-symbol build errors.
 *
 * Initcalls are run by placing pointers in initcall sections that the
 * kernel iterates at runtime. The linker can do dead code / data elimination
 * and remove that completely, so the initcall sections have to be marked
 * as KEEP() in the linker script.
 */

/* Format: <modname>__<counter>_<line>_<fn> */
#define __initcall_id(fn)                                        \
	__PASTE(__KBUILD_MODNAME,                                \
		__PASTE(__, __PASTE(__COUNTER__,                 \
				    __PASTE(_, __PASTE(__LINE__, \
						       __PASTE(_, fn))))))

/* Format: __<prefix>__<iid><id> */
#define __initcall_name(prefix, __iid, id) \
	__PASTE(__, __PASTE(prefix, __PASTE(__, __PASTE(__iid, id))))

#define __initcall_section(__sec, __iid) #__sec ".init"

#define __initcall_stub(fn, __iid, id) fn

#define __define_initcall_stub(__stub, fn) __ADDRESSABLE(fn)

/* clang-format off */
#define ____define_initcall(fn, __stub, __name, __sec)		\
	__define_initcall_stub(__stub, fn)			\
	asm(".section	\"" __sec "\", \"a\"		\n"	\
	    __stringify(__name) ":			\n"	\
	    ".long	" __stringify(__stub) " - .	\n"	\
	    ".previous					\n");	\
	static_assert(__same_type(initcall_t, &fn));
/* clang-format on */

#define __unique_initcall(fn, id, __sec, __iid)                   \
	____define_initcall(fn, __initcall_stub(fn, __iid, id),   \
			    __initcall_name(initcall, __iid, id), \
			    __initcall_section(__sec, __iid))

#define ___define_initcall(fn, id, __sec) \
	__unique_initcall(fn, id, __sec, __initcall_id(fn))

#define __define_initcall(fn, id) ___define_initcall(fn, id, .initcall##id)

#define core_initcall(fn) __define_initcall(fn, 1)
#define fs_initcall(fn) __define_initcall(fn, 5)
#define rootfs_initcall(fn) __define_initcall(fn, rootfs)

#define console_initcall(fn) ___define_initcall(fn, con, .con_initcall)
#endif

#endif /* _LINUX_INIT_H */
