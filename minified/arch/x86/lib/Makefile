# SPDX-License-Identifier: GPL-2.0
#
# Makefile for x86 specific library files.
#

# Produces uninteresting flaky coverage.
KCOV_INSTRUMENT_delay.o	:= n

# KCSAN uses udelay for introducing watchpoint delay; avoid recursion.
KCSAN_SANITIZE_delay.o := n


lib-y := delay.o cpu.o
lib-y += usercopy_$(BITS).o getuser.o putuser.o
lib-y += memcpy_$(BITS).o


ifeq ($(CONFIG_X86_32),y)
        lib-y += atomic64_cx8_32.o
        lib-y += string_32.o
endif
