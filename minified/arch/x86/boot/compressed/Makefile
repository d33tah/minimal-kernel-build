# SPDX-License-Identifier: GPL-2.0
#
# linux/arch/x86/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#
# vmlinuz is:
#	boot code (*.o)
#	asm globals (piggy.S), including:
#		vmlinux.bin.raw
#
# vmlinux.bin is:
#	vmlinux stripped of debugging and comments
# vmlinux.bin.raw is:
#	(see scripts/Makefile.lib size_append)
#	uncompressed vmlinux.bin + u32 size of vmlinux.bin

targets := vmlinux vmlinux.bin vmlinux.bin.raw

# CLANG_FLAGS must come before any cc-disable-warning or cc-option calls: its
# '--target=' flag is needed to avoid errors with '-march=i386'.
KBUILD_CFLAGS := -m$(BITS) -O2 $(CLANG_FLAGS)
KBUILD_CFLAGS += -fno-strict-aliasing -fPIE
KBUILD_CFLAGS += -Wundef
KBUILD_CFLAGS += -DDISABLE_BRANCH_PROFILING
KBUILD_CFLAGS += -march=i386
KBUILD_CFLAGS += -mno-mmx -mno-sse
KBUILD_CFLAGS += -ffreestanding -fshort-wchar
KBUILD_CFLAGS += -fno-stack-protector
KBUILD_CFLAGS += $(call cc-disable-warning, address-of-packed-member)
KBUILD_CFLAGS += $(call cc-disable-warning, gnu)
KBUILD_CFLAGS += -Wno-pointer-sign
KBUILD_CFLAGS += $(call cc-option,-fmacro-prefix-map=$(srctree)/=)
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables
KBUILD_CFLAGS += -D__DISABLE_EXPORTS
# Disable relocation relaxation in case the link is not PIE.
KBUILD_CFLAGS += $(call as-option,-Wa$(comma)-mrelax-relocations=no)
KBUILD_CFLAGS += -include $(srctree)/include/linux/hidden.h

KBUILD_AFLAGS  := $(KBUILD_CFLAGS) -D__ASSEMBLY__

KBUILD_LDFLAGS := -m elf_$(UTS_MACHINE)
KBUILD_LDFLAGS += $(call ld-option,--no-ld-generated-unwind-info)
# Compressed kernel should be built as PIE since it may be loaded at any
# address by the bootloader.
LDFLAGS_vmlinux := -pie $(call ld-option, --no-dynamic-linker)
LDFLAGS_vmlinux += --orphan-handling=warn
LDFLAGS_vmlinux += -T

hostprogs	:= mkpiggy
HOST_EXTRACFLAGS += -I$(srctree)/tools/include

sed-voffset := -e 's/^\([0-9a-fA-F]*\) [ABCDGRSTVW] \(_text\|__bss_start\|_end\)$$/\#define VO_\2 _AC(0x\1,UL)/p'

quiet_cmd_voffset = VOFFSET $@
      cmd_voffset = $(NM) $< | sed -n $(sed-voffset) > $@

targets += ../voffset.h

$(obj)/../voffset.h: vmlinux FORCE
	$(call if_changed,voffset)

$(obj)/misc.o: $(obj)/../voffset.h

vmlinux-objs-y := $(obj)/vmlinux.lds $(obj)/kernel_info.o $(obj)/head_$(BITS).o \
	$(obj)/misc.o $(obj)/string.o $(obj)/error.o \
	$(obj)/piggy.o

$(obj)/vmlinux: $(vmlinux-objs-y) FORCE
	$(call if_changed,ld)

OBJCOPYFLAGS_vmlinux.bin := -O binary
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

targets += $(patsubst $(obj)/%,%,$(vmlinux-objs-y))

quiet_cmd_raw_with_size = RAW     $@
      cmd_raw_with_size = { cat $(real-prereqs); $(size_append); } > $@

$(obj)/vmlinux.bin.raw: $(obj)/vmlinux.bin FORCE
	$(call if_changed,raw_with_size)

quiet_cmd_mkpiggy = MKPIGGY $@
      cmd_mkpiggy = $(obj)/mkpiggy $< > $@

targets += piggy.S
$(obj)/piggy.S: $(obj)/vmlinux.bin.raw $(obj)/mkpiggy FORCE
	$(call if_changed,mkpiggy)
