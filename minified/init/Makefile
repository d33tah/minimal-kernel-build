# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

ccflags-y := -fno-function-sections -fno-data-sections

obj-y                          := main.o version.o do_mounts.o
obj-$(CONFIG_BLK_DEV_INITRD)   += initramfs.o
obj-$(CONFIG_GENERIC_CALIBRATE_DELAY) += calibrate.o

obj-y                          += init_task.o

# dependencies on generated files need to be listed explicitly
$(obj)/version.o: include/generated/compile.h

define filechk_compile.h
	echo '#define UTS_MACHINE "$(UTS_MACHINE)"';		\
	echo '#define UTS_VERSION "#1"';			\
	echo '#define LINUX_COMPILE_BY "kbuild"';		\
	echo '#define LINUX_COMPILE_HOST "localhost"'
endef

include/generated/compile.h: FORCE
	$(call filechk,compile.h)
