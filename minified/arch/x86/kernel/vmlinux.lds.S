/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ld script for the x86 kernel
 *
 * Historic 32-bit version written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 * Modernisation, unification and other changes and fixes:
 *   Copyright (C) 2007-2009  Sam Ravnborg <sam@ravnborg.org>
 *
 *
 * Don't define absolute symbols until and unless you know that symbol
 * value is should remain constant even if kernel image is relocated
 * at run time. Absolute symbols are not relocated. If symbol value should
 * change if kernel is relocated, make the symbol section relative and
 * put it inside the section definition.
 */

/* CONFIG_X86_32 always defined in minimal config */
#define LOAD_OFFSET __PAGE_OFFSET

#define RUNTIME_DISCARD_EXIT
#define EMITS_PT_NOTE
#define RO_EXCEPTION_TABLE_ALIGN	16

#include <asm-generic/vmlinux.lds.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/page_types.h>
/* orc_lookup.h include removed - header was empty */
#include <asm/cache.h>
#include <asm/boot.h>

#undef i386     /* in case the preprocessor is a 32bit one */

OUTPUT_FORMAT(CONFIG_OUTPUT_FORMAT)

/* CONFIG_X86_32 always defined */
OUTPUT_ARCH(i386)
ENTRY(phys_startup_32)

jiffies = jiffies_64;

/* X86_32 - simplified alignment macros */
#define X86_ALIGN_RODATA_BEGIN
#define X86_ALIGN_RODATA_END					\
		. = ALIGN(PAGE_SIZE);				\
		__end_rodata_aligned = .;

#define ALIGN_ENTRY_TEXT_BEGIN
#define ALIGN_ENTRY_TEXT_END
#define BSS_DECRYPTED

PHDRS {
	text PT_LOAD FLAGS(5);          /* R_E */
	data PT_LOAD FLAGS(6);          /* RW_ */
	/* X86_64 percpu/init segments removed */
	note PT_NOTE FLAGS(0);          /* ___ */
}

SECTIONS
{
	/* X86_32 only */
	. = LOAD_OFFSET + LOAD_PHYSICAL_ADDR;
	phys_startup_32 = ABSOLUTE(startup_32 - LOAD_OFFSET);

	/* Text and read-only data */
	.text :  AT(ADDR(.text) - LOAD_OFFSET) {
		_text = .;
		_stext = .;
		/* bootstrapping code */
		HEAD_TEXT
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		ALIGN_ENTRY_TEXT_BEGIN
		ENTRY_TEXT
		ALIGN_ENTRY_TEXT_END
		SOFTIRQENTRY_TEXT
		STATIC_CALL_TEXT
		*(.gnu.warning)
	} :text =0xcccc

	/* End of text section, which should occupy whole number of pages */
	_etext = .;
	. = ALIGN(PAGE_SIZE);

	X86_ALIGN_RODATA_BEGIN
	RO_DATA(PAGE_SIZE)
	X86_ALIGN_RODATA_END

	/* Data */
	.data : AT(ADDR(.data) - LOAD_OFFSET) {
		/* Start of data section */
		_sdata = .;

		/* init_task */
		INIT_TASK_DATA(THREAD_SIZE)

		/* 32 bit has nosave before _edata */
		NOSAVE_DATA

		PAGE_ALIGNED_DATA(PAGE_SIZE)

		CACHELINE_ALIGNED_DATA(L1_CACHE_BYTES)

		DATA_DATA
		CONSTRUCTORS

		/* rarely changed data like cpu maps */
		READ_MOSTLY_DATA(INTERNODE_CACHE_BYTES)

		/* End of data section */
		_edata = .;
	} :data

	BUG_TABLE

	ORC_UNWIND_TABLE

	. = ALIGN(PAGE_SIZE);
	__vvar_page = .;

	.vvar : AT(ADDR(.vvar) - LOAD_OFFSET) {
		/* work around gold bug 13023 */
		__vvar_beginning_hack = .;

		/* Place all vvars at the offsets in asm/vvar.h. */
#define EMIT_VVAR(name, offset)				\
		. = __vvar_beginning_hack + offset;	\
		*(.vvar_ ## name)
#include <asm/vvar.h>
#undef EMIT_VVAR

		/*
		 * Pad the rest of the page with zeros.  Otherwise the loader
		 * can leave garbage here.
		 */
		. = __vvar_beginning_hack + PAGE_SIZE;
	} :data

	. = ALIGN(__vvar_page + PAGE_SIZE, PAGE_SIZE);

	/* Init code and data - will be freed after init */
	. = ALIGN(PAGE_SIZE);
	.init.begin : AT(ADDR(.init.begin) - LOAD_OFFSET) {
		__init_begin = .; /* paired with __init_end */
	}

	/* PERCPU_VADDR removed - X86_32 or !SMP */

	INIT_TEXT_SECTION(PAGE_SIZE)
	/* :init segment removed - X86_32 */

	/*
	 * Section for code used exclusively before alternatives are run. All
	 * references to such code must be patched out by alternatives, normally
	 * by using X86_FEATURE_ALWAYS CPU feature bit.
	 *
	 * See static_cpu_has() for an example.
	 */
	.altinstr_aux : AT(ADDR(.altinstr_aux) - LOAD_OFFSET) {
		*(.altinstr_aux)
	}

	INIT_DATA_SECTION(16)

	.x86_cpu_dev.init : AT(ADDR(.x86_cpu_dev.init) - LOAD_OFFSET) {
		__x86_cpu_dev_start = .;
		*(.x86_cpu_dev.init)
		__x86_cpu_dev_end = .;
	}

	/* CONFIG_X86_INTEL_MID not set - section removed */

	/*
	 * start address and size of operations which during runtime
	 * can be patched with virtualization friendly instructions or
	 * baremetal native ones. Think page table operations.
	 * Details in paravirt_types.h
	 */
	. = ALIGN(8);
	.parainstructions : AT(ADDR(.parainstructions) - LOAD_OFFSET) {
		__parainstructions = .;
		*(.parainstructions)
		__parainstructions_end = .;
	}

	/*
	 * struct alt_inst entries. From the header (alternative.h):
	 * "Alternative instructions for different CPU types or capabilities"
	 * Think locking instructions on spinlocks.
	 */
	. = ALIGN(8);
	.altinstructions : AT(ADDR(.altinstructions) - LOAD_OFFSET) {
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}

	/*
	 * And here are the replacement instructions. The linker sticks
	 * them as binary blobs. The .altinstructions has enough data to
	 * get the address and the length of them to patch the kernel safely.
	 */
	.altinstr_replacement : AT(ADDR(.altinstr_replacement) - LOAD_OFFSET) {
		*(.altinstr_replacement)
	}

	. = ALIGN(8);
	.apicdrivers : AT(ADDR(.apicdrivers) - LOAD_OFFSET) {
		__apicdrivers = .;
		*(.apicdrivers);
		__apicdrivers_end = .;
	}

	. = ALIGN(8);
	/*
	 * .exit.text is discarded at runtime, not link time, to deal with
	 *  references from .altinstructions
	 */
	.exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET) {
		EXIT_TEXT
	}

	.exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET) {
		EXIT_DATA
	}

	/* X86_32 - PERCPU_SECTION is always included */
	PERCPU_SECTION(INTERNODE_CACHE_BYTES)

	. = ALIGN(PAGE_SIZE);

	/* freed after init ends here */
	.init.end : AT(ADDR(.init.end) - LOAD_OFFSET) {
		__init_end = .;
	}

	/*
	 * smp_locks might be freed after init
	 * start/end must be page aligned
	 */
	. = ALIGN(PAGE_SIZE);
	.smp_locks : AT(ADDR(.smp_locks) - LOAD_OFFSET) {
		__smp_locks = .;
		*(.smp_locks)
		. = ALIGN(PAGE_SIZE);
		__smp_locks_end = .;
	}

	/* .data_nosave removed - X86_32 */

	/* BSS */
	. = ALIGN(PAGE_SIZE);
	.bss : AT(ADDR(.bss) - LOAD_OFFSET) {
		__bss_start = .;
		*(.bss..page_aligned)
		. = ALIGN(PAGE_SIZE);
		*(BSS_MAIN)
		BSS_DECRYPTED
		. = ALIGN(PAGE_SIZE);
		__bss_stop = .;
	}

	/*
	 * The memory occupied from _text to here, __end_of_kernel_reserve, is
	 * automatically reserved in setup_arch(). Anything after here must be
	 * explicitly reserved using memblock_reserve() or it will be discarded
	 * and treated as available memory.
	 */
	__end_of_kernel_reserve = .;

	. = ALIGN(PAGE_SIZE);
	.brk : AT(ADDR(.brk) - LOAD_OFFSET) {
		__brk_base = .;
		. += 64 * 1024;		/* 64k alignment slop space */
		*(.bss..brk)		/* areas brk users have reserved */
		__brk_limit = .;
	}

	. = ALIGN(PAGE_SIZE);		/* keep VO_INIT_SIZE page aligned */
	_end = .;

	/* CONFIG_AMD_MEM_ENCRYPT not set - scratch section removed */

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	DISCARDS

	/*
	 * Make sure that the .got.plt is either completely empty or it
	 * contains only the lazy dispatch entries.
	 */
	.got.plt (INFO) : { *(.got.plt) }
	ASSERT(SIZEOF(.got.plt) == 0 ||
	       SIZEOF(.got.plt) == 0xc, /* X86_32 */
	       "Unexpected GOT/PLT entries detected!")

	/*
	 * Sections that should stay zero sized, which is safer to
	 * explicitly check instead of blindly discarding.
	 */
	.got : {
		*(.got) *(.igot.*)
	}
	ASSERT(SIZEOF(.got) == 0, "Unexpected GOT entries detected!")

	.plt : {
		*(.plt) *(.plt.*) *(.iplt)
	}
	ASSERT(SIZEOF(.plt) == 0, "Unexpected run-time procedure linkages detected!")

	.rel.dyn : {
		*(.rel.*) *(.rel_*)
	}
	ASSERT(SIZEOF(.rel.dyn) == 0, "Unexpected run-time relocations (.rel) detected!")

	.rela.dyn : {
		*(.rela.*) *(.rela_*)
	}
	ASSERT(SIZEOF(.rela.dyn) == 0, "Unexpected run-time relocations (.rela) detected!")
}

/*
 * The ASSERT() sink to . is intentional, for binutils 2.14 compatibility:
 */
. = ASSERT((_end - LOAD_OFFSET <= KERNEL_IMAGE_SIZE),
	   "kernel image bigger than KERNEL_IMAGE_SIZE");

/* INIT_PER_CPU and KEXEC sections removed - X86_32, !SMP, !KEXEC */

