#ifndef LINUX_CRASH_CORE_H
#define LINUX_CRASH_CORE_H
#include <linux/linkage.h>
#include <linux/elfcore.h>
#include <linux/elf.h>
#define CRASH_CORE_NOTE_NAME	   "CORE"
#define CRASH_CORE_NOTE_HEAD_BYTES ALIGN(sizeof(struct elf_note), 4)
#define CRASH_CORE_NOTE_NAME_BYTES ALIGN(sizeof(CRASH_CORE_NOTE_NAME), 4)
#define CRASH_CORE_NOTE_DESC_BYTES ALIGN(sizeof(struct elf_prstatus), 4)
#define CRASH_CORE_NOTE_BYTES	   ((CRASH_CORE_NOTE_HEAD_BYTES * 2) + CRASH_CORE_NOTE_NAME_BYTES + CRASH_CORE_NOTE_DESC_BYTES)
#define VMCOREINFO_BYTES	   PAGE_SIZE
#define VMCOREINFO_NOTE_NAME	   "VMCOREINFO"
#define VMCOREINFO_NOTE_NAME_BYTES ALIGN(sizeof(VMCOREINFO_NOTE_NAME), 4)
#define VMCOREINFO_NOTE_SIZE	   ((CRASH_CORE_NOTE_HEAD_BYTES * 2) + VMCOREINFO_NOTE_NAME_BYTES + VMCOREINFO_BYTES)
typedef u32 note_buf_t[CRASH_CORE_NOTE_BYTES/4];
void crash_update_vmcoreinfo_safecopy(void *ptr);
void crash_save_vmcoreinfo(void);
void arch_crash_save_vmcoreinfo(void);
phys_addr_t paddr_vmcoreinfo_note(void);
Elf_Word *append_elf_note(Elf_Word *buf, char *name, unsigned int type, void *data, size_t data_len);
void final_note(Elf_Word *buf);
int __init parse_crashkernel(char *cmdline, unsigned long long system_ram, unsigned long long *crash_size, unsigned long long *crash_base);
int parse_crashkernel_high(char *cmdline, unsigned long long system_ram, unsigned long long *crash_size, unsigned long long *crash_base);
int parse_crashkernel_low(char *cmdline, unsigned long long system_ram, unsigned long long *crash_size, unsigned long long *crash_base);
#endif
