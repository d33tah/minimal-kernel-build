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
/* VMCOREINFO macros, note_buf_t, crash_* functions removed - crash dump not supported */
#endif
