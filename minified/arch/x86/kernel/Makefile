# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

extra-y	:= head_$(BITS).o
extra-y	+= head$(BITS).o
extra-y	+= ebda.o
extra-y	+= platform-quirks.o
extra-y	+= vmlinux.lds

CPPFLAGS_vmlinux.lds += -U$(UTS_MACHINE)

ifdef CONFIG_FUNCTION_TRACER
# Do not profile debug and lowlevel utilities
CFLAGS_REMOVE_tsc.o = -pg
CFLAGS_REMOVE_paravirt-spinlocks.o = -pg
CFLAGS_REMOVE_pvclock.o = -pg
CFLAGS_REMOVE_kvmclock.o = -pg
CFLAGS_REMOVE_ftrace.o = -pg
CFLAGS_REMOVE_early_printk.o = -pg
CFLAGS_REMOVE_head64.o = -pg
CFLAGS_REMOVE_sev.o = -pg
endif

KASAN_SANITIZE_head$(BITS).o				:= n
KASAN_SANITIZE_dumpstack.o				:= n
KASAN_SANITIZE_dumpstack_$(BITS).o			:= n
KASAN_SANITIZE_stacktrace.o				:= n
KASAN_SANITIZE_paravirt.o				:= n
KASAN_SANITIZE_sev.o					:= n

# With some compiler versions the generated code results in boot hangs, caused
# by several compilation units. To be safe, disable all instrumentation.
KCSAN_SANITIZE := n

OBJECT_FILES_NON_STANDARD_test_nx.o			:= y

# If instrumentation of this dir is enabled, boot hangs during first second.
# Probably could be more selective here, but note that files related to irqs,
# boot, dumpstack/stacktrace, etc are either non-interesting or can lead to
# non-deterministic coverage.
KCOV_INSTRUMENT		:= n

CFLAGS_irq.o := -I $(srctree)/$(src)/../include/asm/trace

obj-y			:= process_$(BITS).o signal.o
obj-y			+= traps.o idt.o irq.o irq_$(BITS).o
obj-y			+= time.o dumpstack.o nmi.o
obj-y			+= setup.o x86_init.o i8259.o irqinit.o
obj-y			+= e820.o
obj-y			+= alternative.o i8253.o
obj-y			+= tsc.o io_delay.o
# resource.o removed - arch_remove_reservations never called
obj-y			+= irqflags.o

obj-y				+= process.o
obj-y				+= fpu/
obj-y				+= ptrace.o
# i8237.o removed - empty stub
# obj-y				+= stacktrace.o
obj-y				+= cpu/
obj-y				+= acpi/
obj-y				+= apic/
obj-y				+= kprobes/
obj-$(CONFIG_X86_32)		+= doublefault_32.o
