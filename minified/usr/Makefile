# SPDX-License-Identifier: GPL-2.0
#
# The built-in initramfs: a newc cpio of the files in CONFIG_INITRAMFS_SOURCE
# plus /dev/console, linked into .init.ramfs by initramfs_data.S.
#

obj-$(CONFIG_BLK_DEV_INITRD) := initramfs_data.o

$(obj)/initramfs_data.o: $(obj)/initramfs_data.cpio

hostprogs := gen_init_cpio

# The archived files, listed by gen_initramfs.sh: changing, adding or removing
# one rebuilds the archive.
-include $(obj)/.initramfs_data.cpio.d

# do not try to update files included in initramfs
$(deps_initramfs): ;

quiet_cmd_initfs = GEN     $@
      cmd_initfs = $(CONFIG_SHELL) $< $@ $(obj)/.initramfs_data.cpio.d \
		   $(CONFIG_INITRAMFS_SOURCE)

$(obj)/initramfs_data.cpio: $(src)/gen_initramfs.sh $(obj)/gen_init_cpio $(deps_initramfs) FORCE
	$(call if_changed,initfs)

targets += initramfs_data.cpio
