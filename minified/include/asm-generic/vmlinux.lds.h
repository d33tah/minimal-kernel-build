
#ifndef LOAD_OFFSET
#define LOAD_OFFSET 0
#endif

#ifdef EMITS_PT_NOTE
#define NOTES_HEADERS		:text :note
#define NOTES_HEADERS_RESTORE	__restore_ph : { *(.__restore_ph) } :text
#else
#define NOTES_HEADERS
#define NOTES_HEADERS_RESTORE
#endif

#ifdef RO_EXCEPTION_TABLE_ALIGN
#define RO_EXCEPTION_TABLE	EXCEPTION_TABLE(RO_EXCEPTION_TABLE_ALIGN)
#else
#define RO_EXCEPTION_TABLE
#endif

#define ALIGN_FUNCTION()  . = ALIGN(8)

/* CONFIG_LTO_CLANG is enabled - use expanded section definitions */
#define TEXT_MAIN .text .text.[0-9a-zA-Z_]*
#define DATA_MAIN .data .data.[0-9a-zA-Z_]* .data..L* .data..compoundliteral* .data.$__unnamed_* .data.$L*
#define BSS_MAIN .bss .bss.[0-9a-zA-Z_]* .bss..compoundliteral*

#define STRUCT_ALIGNMENT 32
#define STRUCT_ALIGN() . = ALIGN(STRUCT_ALIGNMENT)

#define SCHED_DATA				\
	STRUCT_ALIGN();				\
	*(__fair_sched_class)			\
	*(__idle_sched_class)

#define MEM_DISCARD(sec) *(.mem##sec)

#define DATA_DATA							\
	*(.xiptext)							\
	*(DATA_MAIN)							\
	*(.ref.data)							\
	*(.data..shared_aligned)  			\
	*(.data.unlikely)						\
	STRUCT_ALIGN();

#define PAGE_ALIGNED_DATA(page_align)					\
	. = ALIGN(page_align);						\
	*(.data..page_aligned)						\
	. = ALIGN(page_align);

#define READ_MOSTLY_DATA(align)						\
	. = ALIGN(align);						\
	*(.data..read_mostly)						\
	. = ALIGN(align);

#define CACHELINE_ALIGNED_DATA(align)					\
	. = ALIGN(align);						\
	*(.data..cacheline_aligned)

#define INIT_TASK_DATA(align)						\
	. = ALIGN(align);						\
	__start_init_task = .;						\
	init_thread_union = .;						\
	init_stack = .;							\
	KEEP(*(.data..init_task))					\
	KEEP(*(.data..init_thread_info))				\
	. = __start_init_task + THREAD_SIZE;				\
	__end_init_task = .;

#ifndef RO_AFTER_INIT_DATA
#define RO_AFTER_INIT_DATA						\
	. = ALIGN(8);							\
	__start_ro_after_init = .;					\
	*(.data..ro_after_init)						\
	__end_ro_after_init = .;
#endif

#define RO_DATA(align)							\
	. = ALIGN((align));						\
	.rodata           : AT(ADDR(.rodata) - LOAD_OFFSET) {		\
		__start_rodata = .;					\
		*(.rodata) *(.rodata.*)					\
		SCHED_DATA						\
		RO_AFTER_INIT_DATA	 	\
	}								\
									\
	RO_EXCEPTION_TABLE						\
	NOTES								\
									\
	. = ALIGN((align));						\
	__end_rodata = .;


#define NOINSTR_TEXT							\
		ALIGN_FUNCTION();					\
		__noinstr_text_start = .;				\
		*(.noinstr.text)					\
		__noinstr_text_end = .;

#define TEXT_TEXT							\
		ALIGN_FUNCTION();					\
		*(.text.hot .text.hot.*)				\
		*(TEXT_MAIN .text.fixup)				\
		*(.text.unlikely .text.unlikely.*)			\
		*(.text.unknown .text.unknown.*)			\
		NOINSTR_TEXT						\
		*(.text..refcount)					\
		*(.ref.text)						\
		*(.text.asan.* .text.tsan.*)				\
	MEM_KEEP(init.text*)						\
	MEM_KEEP(exit.text*)						\


#define SCHED_TEXT							\
		ALIGN_FUNCTION();					\
		__sched_text_start = .;					\
		*(.sched.text)						\
		__sched_text_end = .;

#define ENTRY_TEXT							\
		ALIGN_FUNCTION();					\
		__entry_text_start = .;					\
		*(.entry.text)						\
		__entry_text_end = .;

#define SOFTIRQENTRY_TEXT						\
		ALIGN_FUNCTION();					\
		__softirqentry_text_start = .;				\
		*(.softirqentry.text)					\
		__softirqentry_text_end = .;

#define HEAD_TEXT  KEEP(*(.head.text))

#define EXCEPTION_TABLE(align)						\
	. = ALIGN(align);						\
	__ex_table : AT(ADDR(__ex_table) - LOAD_OFFSET) {		\
		__start___ex_table = .;					\
		KEEP(*(__ex_table))					\
		__stop___ex_table = .;					\
	}

#define INIT_DATA							\
	KEEP(*(SORT(___kentry+*)))					\
	*(.init.data init.data.*)					\
	MEM_DISCARD(init.data*)						\
	*(.init.rodata .init.rodata.*)					\
	MEM_DISCARD(init.rodata)

#define INIT_TEXT							\
	*(.init.text .init.text.*)					\
	*(.text.startup)						\
	MEM_DISCARD(init.text*)



#define ELF_DETAILS							\
		.comment 0 : { *(.comment) }				\
		.symtab 0 : { *(.symtab) }				\
		.strtab 0 : { *(.strtab) }				\
		.shstrtab 0 : { *(.shstrtab) }

#define NOTES								\
	.notes : AT(ADDR(.notes) - LOAD_OFFSET) {			\
		__start_notes = .;					\
		KEEP(*(.note.*))					\
		__stop_notes = .;					\
	} NOTES_HEADERS							\
	NOTES_HEADERS_RESTORE

#define INIT_CALLS_LEVEL(level)						\
		__initcall##level##_start = .;				\
		KEEP(*(.initcall##level##.init))			\
		KEEP(*(.initcall##level##s.init))			\

#define INIT_CALLS							\
		__initcall_start = .;					\
		KEEP(*(.initcallearly.init))				\
		INIT_CALLS_LEVEL(0)					\
		INIT_CALLS_LEVEL(1)					\
		INIT_CALLS_LEVEL(2)					\
		INIT_CALLS_LEVEL(3)					\
		INIT_CALLS_LEVEL(4)					\
		INIT_CALLS_LEVEL(5)					\
		INIT_CALLS_LEVEL(rootfs)				\
		INIT_CALLS_LEVEL(6)					\
		INIT_CALLS_LEVEL(7)					\
		__initcall_end = .;

#define INIT_RAM_FS							\
	. = ALIGN(4);							\
	__initramfs_start = .;						\
	KEEP(*(.init.ramfs))						\
	. = ALIGN(8);							\
	KEEP(*(.init.ramfs.info))

#define COMMON_DISCARDS							\
	*(.discard)							\
	*(.discard.*)							\
	*(.modinfo)							\
	 	\
	*(.gnu.version*)						\

#define DISCARDS							\
	/DISCARD/ : {							\
	COMMON_DISCARDS							\
	}


#define INIT_TEXT_SECTION(inittext_align)				\
	. = ALIGN(inittext_align);					\
	.init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {		\
		_sinittext = .;						\
		INIT_TEXT						\
		_einittext = .;						\
	}

#define INIT_DATA_SECTION(initsetup_align)				\
	.init.data : AT(ADDR(.init.data) - LOAD_OFFSET) {		\
		INIT_DATA						\
		INIT_CALLS						\
		INIT_RAM_FS						\
	}
