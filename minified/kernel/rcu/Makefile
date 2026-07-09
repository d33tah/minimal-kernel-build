# SPDX-License-Identifier: GPL-2.0
# Any varying coverage in these files is non-deterministic
# and is generally not a function of system call inputs.

ifeq ($(CONFIG_KCSAN),y)
KBUILD_CFLAGS += -g -fno-omit-frame-pointer
endif

obj-y += update.o
obj-$(CONFIG_TINY_RCU) += tiny.o
