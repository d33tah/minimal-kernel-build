# SPDX-License-Identifier: GPL-2.0
#
# Makefile for x86 specific library files.
#

# Produces uninteresting flaky coverage.
KCOV_INSTRUMENT_delay.o	:= n

# KCSAN uses udelay for introducing watchpoint delay; avoid recursion.
KCSAN_SANITIZE_delay.o := n
ifdef CONFIG_KCSAN
# In case KCSAN+lockdep+ftrace are enabled, disable ftrace for delay.o to avoid
# lockdep -> [other libs] -> KCSAN -> udelay -> ftrace -> lockdep recursion.
CFLAGS_REMOVE_delay.o = $(CC_FLAGS_FTRACE)
endif

obj-$(CONFIG_SMP) += msr-smp.o cache-smp.o

lib-y := delay.o cpu.o
lib-y += usercopy_$(BITS).o getuser.o putuser.o
lib-y += memcpy_$(BITS).o
lib-$(CONFIG_ARCH_HAS_COPY_MC) += copy_mc.o copy_mc_64.o
lib-$(CONFIG_INSTRUCTION_DECODER) += insn.o inat.o insn-eval.o
lib-$(CONFIG_RANDOMIZE_BASE) += kaslr.o
lib-$(CONFIG_FUNCTION_ERROR_INJECTION)	+= error-inject.o
lib-$(CONFIG_RETPOLINE) += retpoline.o


ifeq ($(CONFIG_X86_32),y)
        lib-y += atomic64_cx8_32.o
        lib-y += string_32.o
endif
