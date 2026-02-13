#ifndef _LINUX_ELF_H
#define _LINUX_ELF_H
#include <linux/types.h>
#include <asm/elf.h>
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
#if ELF_CLASS == ELFCLASS32
#define elfhdr		elf32_hdr
#define elf_phdr	elf32_phdr
#define elf_addr_t	Elf32_Off
#else
#define elfhdr		elf64_hdr
#define elf_phdr	elf64_phdr
#define elf_addr_t	Elf64_Off
#endif
#endif
