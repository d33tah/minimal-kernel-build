# SPDX-License-Identifier: GPL-2.0
# Pre-generated kconfig - no conf binary needed

prebuilt := $(srctree)/scripts/kconfig

PHONY += tinyconfig allnoconfig olddefconfig syncconfig

tinyconfig allnoconfig syncconfig olddefconfig:
	@mkdir -p include/config include/generated
	@cp $(prebuilt)/prebuilt.config .config
	@cp $(prebuilt)/prebuilt.auto.conf include/config/auto.conf
	@printf 'include/config/auto.conf: ;\n' > include/config/auto.conf.cmd
	@cp $(prebuilt)/prebuilt.autoconf.h include/generated/autoconf.h

%.config: ;
