# SPDX-License-Identifier: GPL-2.0
KCOV_INSTRUMENT_tlb.o := n
KCSAN_SANITIZE := n

obj-y := fault.o ioremap.o extable.o pgprot.o

mmu-$(CONFIG_MMU) := init.o init_$(BITS).o pgtable.o tlb.o cpu_entry_area.o

obj-y += $(mmu-y)

CFLAGS_fault.o := -I $(srctree)/$(src)/../include/asm/trace

obj-$(CONFIG_MMU) += pgtable_32.o
