# SPDX-License-Identifier: GPL-2.0
#
# Makefile for x86-compatible CPU details, features and quirks
#

# Don't trace early stages of a secondary CPU boot
ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_common.o = -pg
endif

# If these files are instrumented, boot hangs during the first second.
KCOV_INSTRUMENT_common.o := n

# As above, instrumenting secondary CPU boot code causes boot hangs.
KCSAN_SANITIZE_common.o := n

# Make sure load_percpu_segment has no stackprotector
CFLAGS_common.o		:= -fno-stack-protector

obj-y			:= common.o
obj-y			+= bugs.o
# aperfmperf.o removed - inlined in topology.h
obj-y			+= cpuid-deps.o


# feat_ctl.o removed - empty file
ifdef CONFIG_CPU_SUP_INTEL
endif
# obj-$(CONFIG_CPU_SUP_CYRIX_32)		+= cyrix.o  # deleted
# obj-$(CONFIG_CPU_SUP_CENTAUR)		+= centaur.o  # deleted
# obj-$(CONFIG_CPU_SUP_TRANSMETA_32)	+= transmeta.o  # deleted
# obj-$(CONFIG_CPU_SUP_UMC_32)		+= umc.o  # deleted
# obj-$(CONFIG_CPU_SUP_ZHAOXIN)		+= zhaoxin.o  # deleted
# obj-$(CONFIG_CPU_SUP_VORTEX_32)		+= vortex.o  # deleted



