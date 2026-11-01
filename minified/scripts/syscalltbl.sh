#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Generate the syscall table header: one __SYSCALL(nr, entry) per number,
# sys_ni_syscall for the gaps and for lines without an entry point.
#
# Usage: syscalltbl.sh INFILE OUTFILE
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

nxt=0

grep -E "^[0-9]+[[:space:]]+i386" "$infile" | {
	while read nr abi name native; do
		if [ $nxt -gt $nr ]; then
			echo "error: $infile: syscall table is not sorted or duplicates the same syscall number" >&2
			exit 1
		fi

		while [ $nxt -lt $nr ]; do
			echo "__SYSCALL($nxt, sys_ni_syscall)"
			nxt=$((nxt + 1))
		done

		echo "__SYSCALL($nr, ${native:-sys_ni_syscall})"
		nxt=$((nr + 1))
	done
} > "$outfile"
