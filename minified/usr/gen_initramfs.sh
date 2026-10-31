#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Usage: gen_initramfs.sh <output.cpio> <dep_list> <dir>
#
# Archives the regular files of <dir> (no subdirectories, links or special
# files) with their mode and owner, plus /dev and the /dev/console node, using
# usr/gen_init_cpio. <dep_list> gets the files for kbuild's rebuild check.

set -e

out=$1
dep_list=$2
dir=${3%/}

trap 'rm -f "$out.list"' EXIT
echo "deps_initramfs := $dir \\" > "$dep_list"
for f in $(find "$dir" -mindepth 1 | LC_ALL=C sort); do
	if [ ! -f "$f" ] || [ -L "$f" ]; then
		echo "$0: $f: only regular files are supported" >&2
		exit 1
	fi
	echo "$f \\" >> "$dep_list"
	echo "file /${f#$dir/} $f $(stat -c '%a %u %g' "$f")"
done > "$out.list"
echo "dir /dev 755 1000 1000" >> "$out.list"
echo "nod /dev/console 600 1000 1000 c 5 1" >> "$out.list"
usr/gen_init_cpio < "$out.list" > "$out"
