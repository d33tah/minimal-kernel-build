#ifndef _LINUX_ELF_H
#define _LINUX_ELF_H

#include <asm/elf.h>
#include <uapi/linux/elf.h>

#ifndef START_THREAD
#define START_THREAD(elf_ex, regs, elf_entry, start_stack) \
	start_thread(regs, elf_entry, start_stack)
#endif

#define elfhdr elf32_hdr
#define elf_phdr elf32_phdr
#define elf_addr_t Elf32_Off

#endif
