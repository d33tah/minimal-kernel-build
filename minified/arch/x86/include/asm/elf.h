
#ifndef _ASM_X86_ELF_H
#define _ASM_X86_ELF_H

#include <asm/ptrace.h>

#define elf_check_arch_ia32(x) \
	(((x)->e_machine == EM_386) || ((x)->e_machine == EM_486))

#include <asm/desc.h>

#define elf_check_arch(x) elf_check_arch_ia32(x)

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

#define ELF_HWCAP (boot_cpu_data.x86_capability[CPUID_1_EDX])

extern u32 elf_hwcap2;

#define ELF_HWCAP2 (elf_hwcap2)

#define elf_read_implies_exec(ex, executable_stack) \
	(mmap_is_ia32() && executable_stack == EXSTACK_DEFAULT)

#define ARCH_DLINFO_IA32                                          \
	do {                                                      \
		NEW_AUX_ENT(AT_MINSIGSTKSZ, get_sigframe_size()); \
	} while (0)

static inline int mmap_is_ia32(void)
{
	/* TIF_ADDR32 never set in this build -> COMPAT term always false */
	return IS_ENABLED(CONFIG_X86_32);
}

extern unsigned long get_sigframe_size(void);

#define ARCH_DLINFO ARCH_DLINFO_IA32

#endif
