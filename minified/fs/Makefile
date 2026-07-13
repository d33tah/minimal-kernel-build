# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux filesystems.
#
# 14 Sep 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
# 


obj-y :=	open.o read_write.o file_table.o super.o \
		char_dev.o exec.o namei.o \
		dcache.o inode.o \
		attr.o file.o filesystems.o namespace.o \
		libfs.o \
		fs_struct.o \
		fs_context.o fs_parser.o init.o


obj-$(CONFIG_BINFMT_ELF)	+= binfmt_elf.o



obj-y                          += ramfs/
