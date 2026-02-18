 
#ifndef _ASM_X86_ELF_H
#define _ASM_X86_ELF_H

#include <linux/thread_info.h>

#include <asm/ptrace.h>


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

#define set_personality_64bit()	do { } while (0)

#define ELF_EXEC_PAGESIZE	4096

#define SET_PERSONALITY(ex) set_personality_64bit()

#define elf_read_implies_exec(ex, executable_stack)	\
	(executable_stack == EXSTACK_DEFAULT)

struct linux_binprm;

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
				       int uses_interp);

#endif
