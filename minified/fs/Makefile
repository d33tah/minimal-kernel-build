# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux filesystems.
#

obj-y :=	open.o read_write.o file_table.o super.o \
		exec.o namei.o \
		dcache.o inode.o \
		file.o filesystems.o namespace.o \
		libfs.o \
		fs_struct.o \
		fs_context.o fs_parser.o init.o

obj-$(CONFIG_BINFMT_ELF)	+= binfmt_elf.o

obj-y                          += ramfs/
