# SPDX-License-Identifier: GPL-2.0

extra-y	:= head_$(BITS).o
extra-y	+= head$(BITS).o
extra-y	+= ebda.o
extra-y	+= vmlinux.lds

CPPFLAGS_vmlinux.lds += -U$(UTS_MACHINE)

KCSAN_SANITIZE := n
KCOV_INSTRUMENT := n

CFLAGS_irq.o := -I $(srctree)/$(src)/../include/asm/trace

obj-y			:= process_$(BITS).o signal.o
obj-y			+= traps.o idt.o irq.o irq_$(BITS).o
obj-y			+= time.o dumpstack.o nmi.o
obj-y			+= setup.o x86_init.o i8259.o irqinit.o
obj-y			+= e820.o
obj-y			+= alternative.o
obj-y			+= tsc.o rtc.o
obj-y			+= irqflags.o
obj-y			+= process.o
obj-y			+= fpu/
obj-y			+= ptrace.o
obj-y			+= cpu/
obj-y			+= apic/
obj-$(CONFIG_X86_32)	+= doublefault_32.o
