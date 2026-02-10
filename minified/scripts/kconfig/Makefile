# SPDX-License-Identifier: GPL-2.0
# ===========================================================================
# Kernel configuration targets
# These targets are used from top-level makefile

ifdef KBUILD_KCONFIG
Kconfig := $(KBUILD_KCONFIG)
else
Kconfig := Kconfig
endif

ifndef KBUILD_DEFCONFIG
KBUILD_DEFCONFIG := defconfig
endif

ifeq ($(quiet),silent_)
silent := -s
endif

export KCONFIG_DEFCONFIG_LIST :=
ifndef cross_compiling
kernel-release := $(shell uname -r)
KCONFIG_DEFCONFIG_LIST += \
	/lib/modules/$(kernel-release)/.config \
	/etc/kernel-config \
	/boot/config-$(kernel-release)
endif
KCONFIG_DEFCONFIG_LIST += arch/$(SRCARCH)/configs/$(KBUILD_DEFCONFIG)

# We need this, in case the user has it in its environment
unexport CONFIG_

config-prog	:= conf
menuconfig-prog	:= mconf
nconfig-prog	:= nconf
gconfig-prog	:= gconf
xconfig-prog	:= qconf

define config_rule
PHONY += $(1)
$(1): $(obj)/$($(1)-prog)
	$(Q)$$< $(silent) $(Kconfig)

PHONY += build_$(1)
build_$(1): $(obj)/$($(1)-prog)
endef

$(foreach c, config menuconfig nconfig gconfig xconfig, $(eval $(call config_rule,$(c))))

PHONY += localmodconfig localyesconfig
localyesconfig localmodconfig: $(obj)/conf
	$(Q)$(PERL) $(srctree)/$(src)/streamline_config.pl --$@ $(srctree) $(Kconfig) > .tmp.config
	$(Q)if [ -f .config ]; then 				\
		cmp -s .tmp.config .config ||			\
		(mv -f .config .config.old.1;			\
		 mv -f .tmp.config .config;			\
		 $< $(silent) --oldconfig $(Kconfig);		\
		 mv -f .config.old.1 .config.old)		\
	else							\
		mv -f .tmp.config .config;			\
		$< $(silent) --oldconfig $(Kconfig);		\
	fi
	$(Q)rm -f .tmp.config

# These targets map 1:1 to the commandline options of 'conf'
#
# Note:
#  syncconfig has become an internal implementation detail and is now
#  deprecated for external use
simple-targets := oldconfig allnoconfig allyesconfig allmodconfig \
	alldefconfig randconfig listnewconfig olddefconfig syncconfig \
	helpnewconfig yes2modconfig mod2yesconfig mod2noconfig

PHONY += $(simple-targets)

$(simple-targets): $(obj)/conf
	$(Q)$< $(silent) --$@ $(Kconfig)

PHONY += savedefconfig defconfig

savedefconfig: $(obj)/conf
	$(Q)$< $(silent) --$@=defconfig $(Kconfig)

defconfig: $(obj)/conf
ifneq ($(wildcard $(srctree)/arch/$(SRCARCH)/configs/$(KBUILD_DEFCONFIG)),)
	@$(kecho) "*** Default configuration is based on '$(KBUILD_DEFCONFIG)'"
	$(Q)$< $(silent) --defconfig=arch/$(SRCARCH)/configs/$(KBUILD_DEFCONFIG) $(Kconfig)
else
	@$(kecho) "*** Default configuration is based on target '$(KBUILD_DEFCONFIG)'"
	$(Q)$(MAKE) -f $(srctree)/Makefile $(KBUILD_DEFCONFIG)
endif

%_defconfig: $(obj)/conf
	$(Q)$< $(silent) --defconfig=arch/$(SRCARCH)/configs/$@ $(Kconfig)

configfiles=$(wildcard $(srctree)/kernel/configs/$@ $(srctree)/arch/$(SRCARCH)/configs/$@)

%.config: $(obj)/conf
	$(if $(call configfiles),, $(error No configuration exists for this target on this architecture))
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/kconfig/merge_config.sh -m .config $(configfiles)
	$(Q)$(MAKE) -f $(srctree)/Makefile olddefconfig

PHONY += tinyconfig
tinyconfig:
	$(Q)KCONFIG_ALLCONFIG=kernel/configs/tiny-base.config $(MAKE) -f $(srctree)/Makefile allnoconfig
	$(Q)$(MAKE) -f $(srctree)/Makefile tiny.config

# testconfig, help text, and unused GUI tool targets removed

# ===========================================================================
# object files used by all kconfig flavours
common-objs	:= confdata.o expr.o lexer.lex.o menu.o parser.tab.o \
		   preprocess.o symbol.o util.o

$(obj)/lexer.lex.o: $(obj)/parser.tab.h
HOSTCFLAGS_lexer.lex.o	:= -I $(srctree)/$(src)
HOSTCFLAGS_parser.tab.o	:= -I $(srctree)/$(src)

# conf: Used for defconfig, oldconfig and related targets
hostprogs	+= conf
conf-objs	:= conf.o $(common-objs)

# nconf, mconf, qconf, gconf GUI tools removed - not needed for minimal build
