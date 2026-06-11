# SPDX-License-Identifier: GPL-2.0
OBJECT_FILES_NON_STANDARD := y
CFLAGS_REMOVE_empty.o += $(CC_FLAGS_LTO)

hostprogs-always-y	+= modpost mk_elfconfig
always-y		+= empty.o

modpost-objs	:= modpost.o

# dependencies on generated files need to be listed explicitly

$(obj)/modpost.o: $(obj)/elfconfig.h

quiet_cmd_elfconfig = MKELF   $@
      cmd_elfconfig = $(obj)/mk_elfconfig < $< > $@

$(obj)/elfconfig.h: $(obj)/empty.o $(obj)/mk_elfconfig FORCE
	$(call if_changed,elfconfig)

targets += elfconfig.h
