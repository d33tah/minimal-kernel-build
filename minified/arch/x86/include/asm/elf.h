/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ELF_H
#define _ASM_X86_ELF_H

#include <asm/ptrace.h>

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch_ia32(x) \
	(((x)->e_machine == EM_386) || ((x)->e_machine == EM_486))

#include <asm/desc.h>

#define elf_check_arch(x) elf_check_arch_ia32(x)

/* SVR4/i386 ABI (pages 3-31, 3-32) says that when the program starts %edx
   contains a pointer to a function which might be registered using `atexit'.
   This provides a mean for the dynamic linker to call DT_FINI functions for
   shared libraries that have been loaded before the code runs.

   A value of 0 tells we have no such handler.

   We might as well make sure everything else is cleared too (except for %esp),
   just to make things more deterministic.
 */
#define ELF_PLAT_INIT(_r, load_addr) \
	do {                         \
		_r->bx = 0;          \
		_r->cx = 0;          \
		_r->dx = 0;          \
		_r->si = 0;          \
		_r->di = 0;          \
		_r->bp = 0;          \
		_r->ax = 0;          \
	} while (0)

#define ELF_PLATFORM (utsname()->machine)

#define ELF_EXEC_PAGESIZE 4096

/* This yields a mask that user programs can use to figure out what
   instruction set this CPU supports.  This could be done in user space,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP (boot_cpu_data.x86_capability[CPUID_1_EDX])

extern u32 elf_hwcap2;

/*
 * HWCAP2 supplies mask with kernel enabled CPU features, so that
 * the application can discover that it can safely use them.
 * The bits are defined in uapi/asm/hwcap2.h.
 */
#define ELF_HWCAP2 (elf_hwcap2)

/*
 * An executable for which elf_read_implies_exec() returns TRUE will
 * have the READ_IMPLIES_EXEC personality flag set automatically.
 *
 * The decision process for determining the results are:
 *
 *                 CPU: | lacks NX*  | has NX, ia32     | has NX, x86_64 |
 * ELF:                 |            |                  |                |
 * ---------------------|------------|------------------|----------------|
 * missing PT_GNU_STACK | exec-all   | exec-all         | exec-none      |
 * PT_GNU_STACK == RWX  | exec-stack | exec-stack       | exec-stack     |
 * PT_GNU_STACK == RW   | exec-none  | exec-none        | exec-none      |
 *
 *  exec-all  : all PROT_READ user mappings are executable, except when
 *              backed by files on a noexec-filesystem.
 *  exec-none : only PROT_EXEC user mappings are executable.
 *  exec-stack: only the stack and PROT_EXEC user mappings are executable.
 *
 *  *this column has no architectural effect: NX markings are ignored by
 *   hardware, but may have behavioral effects when "wants X" collides with
 *   "cannot be X" constraints in memory permission flags, as in
 *   https://lkml.kernel.org/r/20190418055759.GA3155@mellanox.com
 *
 */
#define elf_read_implies_exec(ex, executable_stack) \
	(mmap_is_ia32() && executable_stack == EXSTACK_DEFAULT)

/*
 * True on X86_32 or when emulating IA32 on X86_64
 */
static inline int mmap_is_ia32(void)
{
	return IS_ENABLED(CONFIG_X86_32);
}

#endif
