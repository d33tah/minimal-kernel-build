# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the x86 low level entry code
#


CFLAGS_REMOVE_common.o		= $(CC_FLAGS_FTRACE)

CFLAGS_common.o			+= -fno-stack-protector

obj-y				:= entry_$(BITS).o syscall_$(BITS).o
obj-y				+= common.o

obj-y				+= vdso/
