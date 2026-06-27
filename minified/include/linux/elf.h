#ifndef _LINUX_ELF_H
#define _LINUX_ELF_H

#include <linux/types.h>
#include <asm/elf.h>
#include <uapi/linux/elf.h>

#ifndef SET_PERSONALITY2
#define SET_PERSONALITY2(ex, state) \
	SET_PERSONALITY(ex)
#endif

#ifndef START_THREAD
#define START_THREAD(elf_ex, regs, elf_entry, start_stack)	\
	start_thread(regs, elf_entry, start_stack)
#endif

#if defined(ARCH_HAS_SETUP_ADDITIONAL_PAGES) && !defined(ARCH_SETUP_ADDITIONAL_PAGES)
#define ARCH_SETUP_ADDITIONAL_PAGES(bprm, ex, interpreter) \
	arch_setup_additional_pages(bprm, interpreter)
#endif

#define elfhdr		elf32_hdr
#define elf_phdr	elf32_phdr
#define elf_addr_t	Elf32_Off

struct arch_elf_state;

static inline int arch_elf_adjust_prot(int prot,
				       const struct arch_elf_state *state,
				       bool has_interp, bool is_interp)
{
	return prot;
}

#endif  
