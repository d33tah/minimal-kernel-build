# SPDX-License-Identifier: GPL-2.0
out := arch/$(SRCARCH)/include/generated/asm
uapi := arch/$(SRCARCH)/include/generated/uapi/asm

# Create output directory if not already present
$(shell mkdir -p $(out) $(uapi))

syscall32 := $(src)/syscall_32.tbl

syshdr := $(srctree)/scripts/syscallhdr.sh
systbl := $(srctree)/scripts/syscalltbl.sh

quiet_cmd_syshdr = SYSHDR  $@
      cmd_syshdr = $(CONFIG_SHELL) $(syshdr) $< $@
quiet_cmd_systbl = SYSTBL  $@
      cmd_systbl = $(CONFIG_SHELL) $(systbl) $< $@

$(uapi)/unistd_32.h: $(syscall32) $(syshdr) FORCE
	$(call if_changed,syshdr)

$(out)/syscalls_32.h: $(syscall32) $(systbl) FORCE
	$(call if_changed,systbl)

targets += ../../../../$(uapi)/unistd_32.h ../../../../$(out)/syscalls_32.h

PHONY += all
all: $(uapi)/unistd_32.h $(out)/syscalls_32.h
	@:
