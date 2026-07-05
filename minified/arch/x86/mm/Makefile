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

obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_PTDUMP_CORE)	+= dump_pagetables.o
obj-$(CONFIG_PTDUMP_DEBUGFS)	+= debug_pagetables.o

obj-$(CONFIG_HIGHMEM)		+= highmem_32.o

obj-$(CONFIG_KASAN)		+= kasan_init_$(BITS).o

obj-$(CONFIG_NUMA)		+= numa.o numa_$(BITS).o
obj-$(CONFIG_AMD_NUMA)		+= amdtopology.o
obj-$(CONFIG_ACPI_NUMA)		+= srat.o
obj-$(CONFIG_NUMA_EMU)		+= numa_emulation.o

obj-$(CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS)	+= pkeys.o
obj-$(CONFIG_RANDOMIZE_MEMORY)			+= kaslr.o
obj-$(CONFIG_PAGE_TABLE_ISOLATION)		+= pti.o

obj-$(CONFIG_X86_MEM_ENCRYPT)	+= mem_encrypt.o
obj-$(CONFIG_AMD_MEM_ENCRYPT)	+= mem_encrypt_amd.o

obj-$(CONFIG_AMD_MEM_ENCRYPT)	+= mem_encrypt_identity.o
obj-$(CONFIG_AMD_MEM_ENCRYPT)	+= mem_encrypt_boot.o
