#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Generate include/config/auto.conf and include/generated/autoconf.h from
# the frozen .config (replaces kconfig's syncconfig).
set -e
config=${1:-.config}
mkdir -p include/config include/generated
grep '^CONFIG_' "$config" | sed 's/^\(CONFIG_[^=]*\)="\(.*\)"$/\1=\2/' \
	> include/config/auto.conf.tmp
# kconfig prefixes hex values with 0x in autoconf.h; ILLEGAL_POINTER_VALUE is
# the only hex symbol stored without it in .config.
grep '^CONFIG_' "$config" | sed -e 's/^\(CONFIG_[^=]*\)=y$/#define \1 1/' \
	-e 's/^CONFIG_ILLEGAL_POINTER_VALUE=\([0-9]\)/CONFIG_ILLEGAL_POINTER_VALUE=0x\1/' \
	-e 's/^\(CONFIG_[^=]*\)=\(.*\)$/#define \1 \2/' \
	> include/generated/autoconf.h.tmp
mv include/config/auto.conf.tmp include/config/auto.conf
mv include/generated/autoconf.h.tmp include/generated/autoconf.h
