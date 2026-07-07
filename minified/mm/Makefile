# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux memory manager.
#

KASAN_SANITIZE_slab_common.o := n
KASAN_SANITIZE_slub.o := n

# These produce frequent data race reports: most of them are due to races on
# the same word but accesses to different bits of that word. Re-enable KCSAN
# for these when we have more consensus on what to do about them.
KCSAN_SANITIZE_slab_common.o := n
KCSAN_SANITIZE_slub.o := n
KCSAN_SANITIZE_page_alloc.o := n
# But enable explicit instrumentation for memory barriers.
KCSAN_INSTRUMENT_BARRIERS := y

# These files are disabled because they produce non-interesting and/or
# flaky coverage that is not a function of syscall inputs. E.g. slab is out of
# free pages, or a task is migrated between nodes.
KCOV_INSTRUMENT_slab_common.o := n
KCOV_INSTRUMENT_slub.o := n
KCOV_INSTRUMENT_page_alloc.o := n
KCOV_INSTRUMENT_mmzone.o := n

CFLAGS_init-mm.o += $(call cc-disable-warning, override-init)
CFLAGS_init-mm.o += $(call cc-disable-warning, initializer-overrides)

mmu-$(CONFIG_MMU)	:= memory.o \
			   mmap.o mmu_gather.o mremap.o \
			   rmap.o vmalloc.o

obj-y			:= filemap.o \
			   maccess.o folio-compat.o \
			   swap.o truncate.o shmem.o \
			   util.o mmzone.o \
			   percpu.o slab_common.o \
			   interval_tree.o list_lru.o workingset.o \
			   gup.o $(mmu-y)

# Give 'page_alloc' its own module-parameter namespace
page-alloc-y := page_alloc.o

obj-y += page-alloc.o
obj-y += init-mm.o
obj-y += memblock.o

ifdef CONFIG_MMU
endif

# obj-$(CONFIG_HAS_DMA)	+= dmapool.o  # deleted
obj-$(CONFIG_SLUB) += slub.o
obj-$(CONFIG_GENERIC_EARLY_IOREMAP) += early_ioremap.o
