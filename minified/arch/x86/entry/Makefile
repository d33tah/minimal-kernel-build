# SPDX-License-Identifier: GPL-2.0
CFLAGS_REMOVE_common.o = $(CC_FLAGS_FTRACE)
CFLAGS_common.o += -fno-stack-protector
obj-y := entry_$(BITS).o syscall_$(BITS).o common.o
obj-y += vdso/
