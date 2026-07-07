# SPDX-License-Identifier: GPL-2.0
# Kernel does not boot with instrumentation of tlb.c and mem_encrypt*.c
KCOV_INSTRUMENT_tlb.o			:= n

# Disable KCSAN entirely, because otherwise we get warnings that some functions
# reference __initdata sections.
KCSAN_SANITIZE := n

obj-y				:=  init.o init_$(BITS).o fault.o ioremap.o extable.o mmap.o \
				    pgtable.o tlb.o cpu_entry_area.o pgprot.o

obj-y				+= pat/

CFLAGS_fault.o := -I $(srctree)/$(src)/../include/asm/trace

obj-$(CONFIG_X86_32)		+= pgtable_32.o



obj-$(CONFIG_KASAN)		+= kasan_init_$(BITS).o

obj-$(CONFIG_NUMA)		+= numa.o numa_$(BITS).o



