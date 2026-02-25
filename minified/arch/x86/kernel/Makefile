# SPDX-License-Identifier: GPL-2.0

extra-y	:= head_$(BITS).o
extra-y	+= head$(BITS).o
extra-y	+= ebda.o
extra-y	+= vmlinux.lds

CPPFLAGS_vmlinux.lds += -U$(UTS_MACHINE)

KCSAN_SANITIZE := n
KCOV_INSTRUMENT := n

obj-y			:= process_$(BITS).o signal.o
obj-y			+= traps.o idt.o irq_$(BITS).o
obj-y			+= dumpstack.o
obj-y			+= setup.o x86_init.o i8259.o irqinit.o
obj-y			+= e820.o
obj-y			+= tsc.o
obj-y			+= irqflags.o
obj-y			+= process.o
obj-y			+= fpu/
# ptrace.o removed - send_sigtrap inlined into traps.c
obj-y			+= cpu/
obj-y			+= apic/
obj-$(CONFIG_X86_32)	+= doublefault_32.o
