# SPDX-License-Identifier: GPL-2.0
#
# Makefile for x86 specific library files.
#

lib-y := delay.o cpu.o
lib-y += usercopy_$(BITS).o putuser.o
lib-y += memcpy_$(BITS).o

ifeq ($(CONFIG_X86_32),y)
        lib-y += string_32.o
        lib-y += atomic64_386_32.o
endif
