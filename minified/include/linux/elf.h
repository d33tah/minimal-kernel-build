#ifndef _LINUX_ELF_H
#define _LINUX_ELF_H
#include <linux/types.h>
/* asm/elf.h inlined */
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
#include <uapi/linux/elf.h>
#ifndef elf_read_implies_exec
#define elf_read_implies_exec(ex, have_pt_gnu_stack)	0
#endif
#ifndef SET_PERSONALITY
#define SET_PERSONALITY(ex) set_personality(PER_LINUX | (current->personality & (~PER_MASK)))
#endif
#ifndef SET_PERSONALITY2
#define SET_PERSONALITY2(ex, state) SET_PERSONALITY(ex)
#endif
#ifndef START_THREAD
#define START_THREAD(elf_ex, regs, elf_entry, start_stack) start_thread(regs, elf_entry, start_stack)
#endif
#if defined(ARCH_HAS_SETUP_ADDITIONAL_PAGES) && !defined(ARCH_SETUP_ADDITIONAL_PAGES)
#define ARCH_SETUP_ADDITIONAL_PAGES(bprm, ex, interpreter) arch_setup_additional_pages(bprm, interpreter)
#endif
#define elfhdr		elf32_hdr
#define elf_phdr	elf32_phdr
#define elf_addr_t	Elf32_Off
#endif
