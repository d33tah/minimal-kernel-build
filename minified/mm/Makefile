# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux memory manager.
#

CFLAGS_init-mm.o += $(call cc-disable-warning, override-init)
CFLAGS_init-mm.o += $(call cc-disable-warning, initializer-overrides)

mmu-$(CONFIG_MMU)	:= memory.o \
			   mmap.o mmu_gather.o \
			   rmap.o

obj-y			:= filemap.o \
			   swap.o \
			   util.o mmzone.o \
			   percpu.o slab_common.o \
			   gup.o $(mmu-y)

obj-y += page_alloc.o
obj-y += init-mm.o
obj-y += memblock.o
obj-$(CONFIG_SLUB) += slub.o
