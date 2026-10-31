#ifndef _UAPI_LINUX_ELF_H
#define _UAPI_LINUX_ELF_H

#include <linux/types.h>

#define EM_386 3
#define EM_486 6

typedef __u32 Elf32_Addr;
typedef __u16 Elf32_Half;
typedef __u32 Elf32_Off;
typedef __u32 Elf32_Word;

#define PT_LOAD 1
#define PT_NOTE 4

#define ET_EXEC 2
/* ET_CORE, ET_LOPROC, ET_HIPROC - unused */

/* Dynamic section tags - only keep those used */

#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2

#define EI_NIDENT 16

typedef struct elf32_hdr {
	unsigned char e_ident[EI_NIDENT];
	Elf32_Half e_type;
	Elf32_Half e_machine;
	Elf32_Word e_version;
	Elf32_Addr e_entry;
	Elf32_Off e_phoff;
	Elf32_Off e_shoff;
	Elf32_Word e_flags;
	Elf32_Half e_ehsize;
	Elf32_Half e_phentsize;
	Elf32_Half e_phnum;
	Elf32_Half e_shentsize;
	Elf32_Half e_shnum;
	Elf32_Half e_shstrndx;
} Elf32_Ehdr;

#define PF_R 0x4
#define PF_W 0x2
#define PF_X 0x1

typedef struct elf32_phdr {
	Elf32_Word p_type;
	Elf32_Off p_offset;
	Elf32_Addr p_vaddr;
	Elf32_Addr p_paddr;
	Elf32_Word p_filesz;
	Elf32_Word p_memsz;
	Elf32_Word p_flags;
	Elf32_Word p_align;
} Elf32_Phdr;

/* SHT_PROGBITS(1), SHT_NOBITS(8) removed - 0-caller section types */

/* SHF_EXECINSTR(0x4) removed - 0-caller section flag */

/* SHN_LORESERVE, SHN_HIRESERVE, SHN_LOPROC, SHN_HIPROC - unused, removed */

#define ELFMAG "\177ELF"
#define SELFMAG 4

/* ELFDATA2MSB(2) removed - 0-caller (this tree is little-endian, uses ELFDATA2LSB) */

#endif
