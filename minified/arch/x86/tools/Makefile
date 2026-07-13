# SPDX-License-Identifier: GPL-2.0
HOST_EXTRACFLAGS += -I$(srctree)/tools/include
hostprogs	+= relocs
relocs-objs     := relocs_32.o relocs_common.o
PHONY += relocs
relocs: $(obj)/relocs
	@:
