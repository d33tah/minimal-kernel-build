# SPDX-License-Identifier: GPL-2.0
#
# Makefile for x86 specific library files.
#

KCOV_INSTRUMENT_delay.o := n
KCSAN_SANITIZE_delay.o := n

lib-y := delay.o cpu.o
lib-y += usercopy_$(BITS).o getuser.o putuser.o
lib-y += memcpy_$(BITS).o

ifeq ($(CONFIG_X86_32),y)
        lib-y += string_32.o
ifeq ($(CONFIG_X86_CMPXCHG64),y)
        lib-y += atomic64_cx8_32.o
else
        lib-y += cmpxchg8b_emu.o atomic64_386_32.o
endif
endif
