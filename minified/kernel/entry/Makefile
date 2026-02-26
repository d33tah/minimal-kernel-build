# SPDX-License-Identifier: GPL-2.0
CFLAGS_REMOVE_common.o = -fstack-protector -fstack-protector-strong
CFLAGS_common.o += -fno-stack-protector
obj-$(CONFIG_GENERIC_ENTRY) += common.o
