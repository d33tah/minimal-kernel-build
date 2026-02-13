 
#ifndef _ASM_X86_ELF_H
#define _ASM_X86_ELF_H

#include <linux/thread_info.h>

#include <asm/ptrace.h>
/* auxvec.h inlined */
#define AT_SYSINFO		32
#define AT_SYSINFO_EHDR		33

#define R_386_NONE	0
#define R_386_32	1
#define R_386_PC32	2
#define R_386_PLT32	4
#define R_386_RELATIVE	8

#define ELF_CLASS	ELFCLASS32

#include <asm/vdso.h>

extern unsigned int vdso32_enabled;

#define elf_check_arch_ia32(x) \
	(((x)->e_machine == EM_386) || ((x)->e_machine == EM_486))

#include <asm/processor.h>

#include <asm/desc.h>

#define elf_check_arch(x)	elf_check_arch_ia32(x)

#define ELF_PLAT_INIT(_r, load_addr)		\
	do {					\
	_r->bx = 0; _r->cx = 0; _r->dx = 0;	\
	_r->si = 0; _r->di = 0; _r->bp = 0;	\
	_r->ax = 0;				\
} while (0)

#define ELF_PLATFORM	(utsname()->machine)
#define set_personality_64bit()	do { } while (0)

#define ELF_EXEC_PAGESIZE	4096

#define ELF_HWCAP		(boot_cpu_data.x86_capability[CPUID_1_EDX])

extern u32 elf_hwcap2;

#define ELF_HWCAP2		(elf_hwcap2)

#define SET_PERSONALITY(ex) set_personality_64bit()

#define elf_read_implies_exec(ex, executable_stack)	\
	(mmap_is_ia32() && executable_stack == EXSTACK_DEFAULT)

#define	ARCH_DLINFO_IA32						\
do {									\
	if (VDSO_CURRENT_BASE) {					\
		NEW_AUX_ENT(AT_SYSINFO,	VDSO_ENTRY);			\
		NEW_AUX_ENT(AT_SYSINFO_EHDR, VDSO_CURRENT_BASE);	\
	}								\
	NEW_AUX_ENT(AT_MINSIGSTKSZ, get_sigframe_size());		\
} while (0)

/* CONFIG_X86_32 is enabled, always ia32 */
static inline int mmap_is_ia32(void)
{
	return 1;
}

extern unsigned long task_size_64bit(int full_addr_space);
extern unsigned long get_sigframe_size(void);

#define ARCH_DLINFO		ARCH_DLINFO_IA32

#define VDSO_CURRENT_BASE	((unsigned long)current->mm->context.vdso)

#define VDSO_ENTRY							\
	((unsigned long)current->mm->context.vdso +			\
	 vdso_image_32.sym___kernel_vsyscall)

struct linux_binprm;

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
				       int uses_interp);

#endif
