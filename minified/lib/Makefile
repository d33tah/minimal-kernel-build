# SPDX-License-Identifier: GPL-2.0
#
# Makefile for some libs needed in the kernel.
#

# string.o implements standard library functions like memset/memcpy etc.
# Use -ffreestanding to ensure that the compiler does not try to "optimize"
# them into calls to themselves.
CFLAGS_string.o := -ffreestanding

lib-y := ctype.o string.o vsprintf.o \
	 rbtree.o radix-tree.o xarray.o \
	 idr.o extable.o \
	 dec_and_lock.o

lib-y	+= kobject.o klist.o
obj-y	+= lockref.o

obj-y += sort.o \
	 iov_iter.o \
	 find_bit.o \
	 refcount.o
# string_helpers.o removed - functions inlined into callers
obj-y += kstrtox.o

obj-y += math/ crypto/

obj-$(CONFIG_GENERIC_STRNLEN_USER) += strnlen_user.o
