# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

ccflags-y := -fno-function-sections -fno-data-sections

obj-y                          := main.o version.o mounts.o
# initramfs.o removed - empty, merged into do_mounts.c
# noinitramfs.o merged into do_mounts.c
obj-$(CONFIG_GENERIC_CALIBRATE_DELAY) += calibrate.o

obj-y                          += init_task.o

mounts-y			:= do_mounts.o
mounts-$(CONFIG_BLK_DEV_RAM)	+= do_mounts_rd.o
# do_mounts_initrd.o merged into do_mounts.c

# dependencies on generated files need to be listed explicitly
$(obj)/version.o: include/generated/compile.h

# compile.h changes depending on hostname, generation number, etc,
# so we regenerate it always.
# mkcompile_h will make sure to only update the
# actual file if its content has changed.

quiet_cmd_compile.h = CHK     $@
      cmd_compile.h = \
	$(CONFIG_SHELL) $(srctree)/scripts/mkcompile_h $@	\
	"$(UTS_MACHINE)" "$(CONFIG_SMP)" "$(CONFIG_PREEMPT_BUILD)"	\
	"$(CONFIG_PREEMPT_DYNAMIC)" "$(CONFIG_PREEMPT_RT)" \
	"$(CONFIG_CC_VERSION_TEXT)" "$(LD)"

include/generated/compile.h: FORCE
	$(call cmd,compile.h)
