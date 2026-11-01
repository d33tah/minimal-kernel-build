#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Generate the syscall number header (__NR_<name> and __NR_syscalls).
#
# Usage: syscallhdr.sh INFILE OUTFILE
#
# Each line of the syscall table should have the following format:
#
# NR ABI NAME [NATIVE]
#
# NR       syscall number
# ABI      ABI name (i386)
# NAME     syscall name
# NATIVE   native entry point (optional)

set -e

infile="$1"
outfile="$2"

guard=_UAPI_ASM_$(basename "$outfile" |
	sed -e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/' \
	-e 's/[^A-Z0-9_]/_/g' -e 's/__/_/g')

grep -E "^[0-9]+[[:space:]]+i386" "$infile" | {
	echo "#ifndef $guard"
	echo "#define $guard"
	echo

	max=0
	while read nr abi name native; do
		max=$nr
		echo "#define __NR_$name $nr"
	done

	echo
	echo "#ifdef __KERNEL__"
	echo "#define __NR_syscalls $(($max + 1))"
	echo "#endif"

	echo
	echo "#endif /* $guard */"
} > "$outfile"
