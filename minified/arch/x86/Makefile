# SPDX-License-Identifier: GPL-2.0
# x86_32 (CONFIG_X86_32), built with clang

# Align the stack to the register width instead of the default 16 bytes.
cc_stack_align4 := -mstack-alignment=4

# How to compile the 16-bit code.  Note we always compile for -march=i386;
# that way we can complain to the user if the CPU is insufficient.
REALMODE_CFLAGS	:= -m16 -g -Os -DDISABLE_BRANCH_PROFILING -D__DISABLE_EXPORTS \
		   -Wall -Wstrict-prototypes -march=i386 -mregparm=3 \
		   -fno-strict-aliasing -fomit-frame-pointer -fno-pic \
		   -mno-mmx -mno-sse $(call cc-option,-fcf-protection=none)

REALMODE_CFLAGS += -ffreestanding
REALMODE_CFLAGS += -fno-stack-protector
REALMODE_CFLAGS += -Wno-address-of-packed-member
REALMODE_CFLAGS += $(cc_stack_align4)
REALMODE_CFLAGS += $(CLANG_FLAGS)
export REALMODE_CFLAGS

# BITS is used as extension for files which are available in a 32 bit
# and a 64 bit version to simplify shared Makefiles.
# e.g.: obj-y += foo_$(BITS).o
BITS := 32
export BITS

UTS_MACHINE := i386

# Prevent the compiler from generating any FP code by mistake.
KBUILD_CFLAGS += -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx

KBUILD_CFLAGS += $(call cc-option,-fcf-protection=none)

KBUILD_AFLAGS += -m32
KBUILD_CFLAGS += -m32

KBUILD_CFLAGS += -msoft-float -mregparm=3 -freg-struct-return

# Never want PIC in a 32-bit kernel
KBUILD_CFLAGS += -fno-pic

KBUILD_CFLAGS += $(cc_stack_align4)

# CPU-specific tuning (CONFIG_M686 etc.)
include $(srctree)/arch/x86/Makefile_32.cpu
KBUILD_CFLAGS += $(cflags-y)

# temporary until string.h is fixed
KBUILD_CFLAGS += -ffreestanding

KBUILD_CFLAGS += -Wno-sign-compare
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables

KBUILD_LDFLAGS += -m elf_$(UTS_MACHINE)

###
# Syscall table generation

archheaders:
	$(Q)$(MAKE) $(build)=arch/x86/entry/syscalls all

###
# Kernel objects

head-y := arch/x86/kernel/head_$(BITS).o
head-y += arch/x86/kernel/head$(BITS).o

libs-y  += arch/x86/lib/

boot := arch/x86/boot

PHONY += bzImage

# Default kernel to build
all: bzImage

bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/bzImage

