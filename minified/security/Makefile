# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the kernel security code
#

# commoncap.o removed - all functions were stubs never called
# min_addr.o removed - mmap_min_addr moved to mm/mmap.c

# Object file lists
obj-$(CONFIG_SECURITY)			+= security.o
obj-$(CONFIG_SECURITYFS)		+= inode.o
obj-$(CONFIG_SECURITY)			+= lsm_audit.o
obj-$(CONFIG_CGROUPS)			+= device_cgroup.o

