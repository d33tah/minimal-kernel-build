# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

extra-y	:= head_$(BITS).o
extra-y	+= head$(BITS).o
extra-y	+= vmlinux.lds

CPPFLAGS_vmlinux.lds += -U$(UTS_MACHINE)

obj-y			:= process_$(BITS).o
obj-y			+= traps.o idt.o irq.o irq_$(BITS).o
obj-y			+= time.o dumpstack.o nmi.o
obj-y			+= setup.o i8259.o irqinit.o
obj-y			+= e820.o
obj-y			+= i8253.o
obj-y			+= tsc.o io_delay.o

obj-y				+= process.o
obj-y				+= cpu/
obj-$(CONFIG_X86_32)		+= doublefault_32.o
