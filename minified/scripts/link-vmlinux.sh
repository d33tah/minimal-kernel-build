#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# link vmlinux

set -e

LD="$1"
KBUILD_LDFLAGS="$2"
LDFLAGS_vmlinux="$3"

is_enabled() {
	grep -q "^$1=y" include/config/auto.conf
}

info()
{
	printf "  %-7s %s\n" "${1}" "${2}"
}

vmlinux_link()
{
	local output=${1}
	local objs
	local libs
	local ld
	local ldflags
	local ldlibs

	info LD ${output}

	shift

	if is_enabled CONFIG_LTO_CLANG || is_enabled CONFIG_X86_KERNEL_IBT; then
		objs=vmlinux.o
		libs=
	else
		objs="${KBUILD_VMLINUX_OBJS}"
		libs="${KBUILD_VMLINUX_LIBS}"
	fi

	ld="${LD}"
	ldflags="${KBUILD_LDFLAGS} ${LDFLAGS_vmlinux}"
	ldlibs=

	ldflags="${ldflags} --script=${objtree}/${KBUILD_LDS}"

	${ld} ${ldflags} -o ${output}					\
		--whole-archive ${objs} --no-whole-archive		\
		--start-group ${libs} --end-group			\
		$@ ${ldlibs}
}

mksysmap()
{
	${CONFIG_SHELL} "${srctree}/scripts/mksysmap" ${1} ${2}
}

sorttable()
{
	${objtree}/scripts/sorttable ${1}
}

cleanup()
{
	rm -f System.map
	rm -f vmlinux
	rm -f vmlinux.map
	rm -f .vmlinux.objs
}

case "${KBUILD_VERBOSE}" in
*1*)
	set -x
	;;
esac

if [ "$1" = "clean" ]; then
	cleanup
	exit 0
fi

info GEN .version
if [ -r .version ]; then
	VERSION=$(expr 0$(cat .version) + 1)
	echo $VERSION > .version
else
	rm -f .version
	echo 1 > .version
fi;

${MAKE} -f "${srctree}/scripts/Makefile.build" obj=init need-builtin=1

${MAKE} -f "${srctree}/scripts/Makefile.vmlinux_o"

for f in ${KBUILD_VMLINUX_OBJS} ${KBUILD_VMLINUX_LIBS}; do
	case ${f} in
	*.a)
		${AR} t ${f} ;;
	*)
		echo ${f} ;;
	esac
done > .vmlinux.objs

${MAKE} -f "${srctree}/scripts/Makefile.modpost" MODPOST_VMLINUX=1

info MODINFO modules.builtin.modinfo
${OBJCOPY} -j .modinfo -O binary vmlinux.o modules.builtin.modinfo
info GEN modules.builtin
tr '\0' '\n' < modules.builtin.modinfo | sed -n 's/^[[:alnum:]:_]*\.file=//p' |
	tr ' ' '\n' | uniq | sed -e 's:^:kernel/:' -e 's/$/.ko/' > modules.builtin

vmlinux_link vmlinux "" ""

info SYSMAP System.map
mksysmap vmlinux System.map

if is_enabled CONFIG_BUILDTIME_TABLE_SORT; then
	info SORTTAB vmlinux
	if ! sorttable vmlinux; then
		echo >&2 Failed to sort kernel tables
		exit 1
	fi
fi

echo "vmlinux: $0" > .vmlinux.d
