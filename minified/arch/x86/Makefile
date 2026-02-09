# SPDX-License-Identifier: GPL-2.0
# Unified Makefile for i386 and x86_64

ifeq ($(ARCH),x86)
  ifeq ($(shell uname -m),x86_64)
        KBUILD_DEFCONFIG := x86_64_defconfig
  else
        KBUILD_DEFCONFIG := i386_defconfig
  endif
else
        KBUILD_DEFCONFIG := $(ARCH)_defconfig
endif

ifdef CONFIG_CC_IS_CLANG
RETPOLINE_CFLAGS	:= -mretpoline-external-thunk
RETPOLINE_VDSO_CFLAGS	:= -mretpoline
endif

export RETPOLINE_CFLAGS
export RETPOLINE_VDSO_CFLAGS

ifneq ($(call cc-option, -mpreferred-stack-boundary=4),)
      cc_stack_align4 := -mpreferred-stack-boundary=2
      cc_stack_align8 := -mpreferred-stack-boundary=3
else ifneq ($(call cc-option, -mstack-alignment=16),)
      cc_stack_align4 := -mstack-alignment=4
      cc_stack_align8 := -mstack-alignment=8
endif

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

export BITS

KBUILD_CFLAGS += -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx
KBUILD_CFLAGS += $(call cc-option,-fcf-protection=none)

ifeq ($(CONFIG_X86_32),y)
        BITS := 32
        UTS_MACHINE := i386
        CHECKFLAGS += -D__i386__

        KBUILD_AFLAGS += -m32
        KBUILD_CFLAGS += -m32

        KBUILD_CFLAGS += -msoft-float -mregparm=3 -freg-struct-return
        KBUILD_CFLAGS += -fno-pic
        KBUILD_CFLAGS += $(cc_stack_align4)

        include $(srctree)/arch/x86/Makefile_32.cpu
        KBUILD_CFLAGS += $(cflags-y)

        KBUILD_CFLAGS += -ffreestanding

	ifeq ($(CONFIG_STACKPROTECTOR),y)
		KBUILD_CFLAGS += -mstack-protector-guard=global
	endif
else
        BITS := 64
        UTS_MACHINE := x86_64
        CHECKFLAGS += -D__x86_64__

        KBUILD_AFLAGS += -m64
        KBUILD_CFLAGS += -m64
        KBUILD_CFLAGS += $(call cc-option,-falign-jumps=1)
        KBUILD_CFLAGS += $(call cc-option,-falign-loops=1)
        KBUILD_CFLAGS += -mno-80387
        KBUILD_CFLAGS += $(call cc-option,-mno-fp-ret-in-387)
        KBUILD_CFLAGS += $(cc_stack_align8)
	KBUILD_CFLAGS += $(call cc-option,-mskip-rax-setup)

        cflags-$(CONFIG_GENERIC_CPU)	+= -mtune=generic
        KBUILD_CFLAGS += $(cflags-y)

        KBUILD_CFLAGS += -mno-red-zone
        KBUILD_CFLAGS += -mcmodel=kernel
endif

KBUILD_CFLAGS += -Wno-sign-compare
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables

ifdef CONFIG_RETPOLINE
  KBUILD_CFLAGS += $(RETPOLINE_CFLAGS)
endif

KBUILD_LDFLAGS += -m elf_$(UTS_MACHINE)

ifdef CONFIG_X86_NEED_RELOCS
LDFLAGS_vmlinux := --emit-relocs --discard-none
else
LDFLAGS_vmlinux :=
endif

archscripts: scripts_basic
	$(Q)$(MAKE) $(build)=arch/x86/tools relocs

archheaders:
	$(Q)$(MAKE) $(build)=arch/x86/entry/syscalls all

head-y := arch/x86/kernel/head_$(BITS).o
head-y += arch/x86/kernel/head$(BITS).o
head-y += arch/x86/kernel/ebda.o

libs-y  += arch/x86/lib/

boot := arch/x86/boot

PHONY += bzImage

all: bzImage

KBUILD_IMAGE := $(boot)/bzImage

bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(KBUILD_IMAGE)
	$(Q)mkdir -p $(objtree)/arch/$(UTS_MACHINE)/boot
	$(Q)ln -fsn ../../x86/boot/bzImage $(objtree)/arch/$(UTS_MACHINE)/boot/$@

archprepare: checkbin
checkbin:
ifndef CONFIG_CC_HAS_ASM_GOTO
	@echo Compiler lacks asm-goto support.
	@exit 1
endif

archclean:
	$(Q)rm -rf $(objtree)/arch/i386
	$(Q)rm -rf $(objtree)/arch/x86_64
