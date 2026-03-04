# SPDX-License-Identifier: GPL-2.0
# Unified Makefile for i386 and x86_64

KBUILD_DEFCONFIG := i386_defconfig

ifneq ($(call cc-option, -mpreferred-stack-boundary=4),)
      cc_stack_align4 := -mpreferred-stack-boundary=2
else ifneq ($(call cc-option, -mstack-alignment=16),)
      cc_stack_align4 := -mstack-alignment=4
endif

REALMODE_CFLAGS	:= -m16 -g -Os -DDISABLE_BRANCH_PROFILING -D__DISABLE_EXPORTS \
		   -Wall -Wstrict-prototypes -march=i386 -mregparm=3 \
		   -fno-strict-aliasing -fomit-frame-pointer -fno-pic \
		   -mno-mmx -mno-sse $(call cc-option,-fcf-protection=none)
REALMODE_CFLAGS += -ffreestanding -fno-stack-protector -Wno-address-of-packed-member
REALMODE_CFLAGS += $(cc_stack_align4) $(CLANG_FLAGS)
export REALMODE_CFLAGS

BITS := 32
UTS_MACHINE := i386
export BITS

KBUILD_CFLAGS += -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx
KBUILD_CFLAGS += $(call cc-option,-fcf-protection=none)
KBUILD_AFLAGS += -m32
KBUILD_CFLAGS += -m32
KBUILD_CFLAGS += -msoft-float -mregparm=3 -freg-struct-return
KBUILD_CFLAGS += -fno-pic $(cc_stack_align4)
include $(srctree)/arch/x86/Makefile_32.cpu
KBUILD_CFLAGS += $(cflags-y) -ffreestanding
KBUILD_CFLAGS += -Wno-sign-compare -fno-asynchronous-unwind-tables

KBUILD_LDFLAGS += -m elf_$(UTS_MACHINE)
LDFLAGS_vmlinux :=

archscripts:

archheaders:
	@mkdir -p arch/x86/include/generated/uapi/asm arch/x86/include/generated/asm
	@printf '#ifndef _UAPI_ASM_UNISTD_32_H\n#define _UAPI_ASM_UNISTD_32_H\n#define __NR_ni_syscall 0\n#define __NR_exit 1\n#define __NR_write 4\n#ifdef __KERNEL__\n#define __NR_syscalls 5\n#endif\n#endif\n' > arch/x86/include/generated/uapi/asm/unistd_32.h
	@printf '__SYSCALL(0, sys_ni_syscall)\n__SYSCALL(1, sys_exit)\n__SYSCALL(2, sys_ni_syscall)\n__SYSCALL(3, sys_ni_syscall)\n__SYSCALL(4, sys_write)\n' > arch/x86/include/generated/asm/syscalls_32.h

head-y := arch/x86/kernel/head_$(BITS).o
head-y += arch/x86/kernel/head$(BITS).o

libs-y  += arch/x86/lib/

boot := arch/x86/boot

PHONY += bzImage
all: bzImage
KBUILD_IMAGE := $(boot)/bzImage

bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(KBUILD_IMAGE)
	$(Q)mkdir -p $(objtree)/arch/$(UTS_MACHINE)/boot
	$(Q)ln -fsn ../../x86/boot/bzImage $(objtree)/arch/$(UTS_MACHINE)/boot/$@

archprepare:
archclean:
	$(Q)rm -rf $(objtree)/arch/i386
