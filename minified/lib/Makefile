# SPDX-License-Identifier: GPL-2.0
#
# Makefile for some libs needed in the kernel.
#

# string.o implements standard library functions like memset/memcpy etc.
# Use -ffreestanding to ensure that the compiler does not try to "optimize"
# them into calls to themselves.

lib-y := string.o xarray.o irq_regs.o

obj-y += lockref.o iov_iter.o string_helpers.o

obj-$(CONFIG_GENERIC_STRNLEN_USER) += strnlen_user.o
