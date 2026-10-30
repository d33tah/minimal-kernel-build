#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# link vmlinux
#
# CONFIG_LTO_CLANG is set, so vmlinux is linked from vmlinux.o (the LTO
# link of $(KBUILD_VMLINUX_OBJS) and $(KBUILD_VMLINUX_LIBS)).

# Error out on error
set -e

LD="$1"
KBUILD_LDFLAGS="$2"
LDFLAGS_vmlinux="$3"

# Nice output in kbuild format
# Will be supressed by "make -s"
info()
{
	printf "  %-7s %s\n" "${1}" "${2}"
}

# Use "make V=1" to debug this script
case "${KBUILD_VERBOSE}" in
*1*)
	set -x
	;;
esac

if [ "$1" = "clean" ]; then
	rm -f vmlinux
	exit 0
fi

#link vmlinux.o
${MAKE} -f "${srctree}/scripts/Makefile.vmlinux_o"

info LD vmlinux
${LD} ${KBUILD_LDFLAGS} ${LDFLAGS_vmlinux} --script=${objtree}/${KBUILD_LDS} \
	-o vmlinux --whole-archive vmlinux.o --no-whole-archive

# For fixdep
echo "vmlinux: $0" > .vmlinux.d
