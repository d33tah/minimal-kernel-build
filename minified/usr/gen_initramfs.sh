#!/bin/sh
# Copyright (C) Martin Schlemmer <azarah@nosferatu.za.org>
# Copyright (C) 2006 Sam Ravnborg <sam@ravnborg.org>
#
# Released under the terms of the GNU GPL
#
# Generate a cpio packed initramfs. It uses gen_init_cpio to generate
# the cpio archive.
# This script assumes that gen_init_cpio is located in usr/ directory

# error out on errors
set -e

# awk style field access
# $1 - field number; rest is argument string
field() {
	shift $1 ; echo $1
}

filetype() {
	local argv1="$1"

	if [ -f "${argv1}" ]; then
		echo "file"
	elif [ -d "${argv1}" ]; then
		echo "dir"
	else
		echo "invalid"
	fi
	return 0
}

# for each file print a line in following format
# <filetype> <name> <path to file> <octal mode> <uid> <gid>
parse() {
	local location="$1"
	local name="/${location#${srcdir}}"
	# change '//' into '/'
	name=$(echo "$name" | sed -e 's://*:/:g')
	local mode="$2"
	local uid="$3"
	local gid="$4"
	local ftype=$(filetype "${location}")
	local str="${mode} ${uid} ${gid}"

	[ "${ftype}" = "invalid" ] && return 0
	[ "${location}" = "${srcdir}" ] && return 0

	case "${ftype}" in
		"file")
			str="${ftype} ${name} ${location} ${str}"
			;;
		*)
			str="${ftype} ${name} ${str}"
			;;
	esac

	echo "${str}" >> $cpio_list

	return 0
}

# process one directory (incl sub-directories)
dir_filelist() {
	srcdir=$(echo "$1" | sed -e 's://*:/:g')
	dirlist=$(find "${srcdir}" -printf "%p %m %U %G\n" | LC_ALL=C sort)

	# If $dirlist is only one line, then the directory is empty
	if [  "$(echo "${dirlist}" | wc -l)" -gt 1 ]; then
		echo "${dirlist}" | \
		while read x; do
			parse $x
		done

        # Added by d33tah: always include /dev/console. This means that
        # the initramfs will always have a console, even if the user
        # doesn't include it in the list of files.
        #
        # This way we don't need to be root to build the initramfs.
	    echo "dir /dev 755 1000 1000" >> $cpio_list
        echo "nod /dev/console 600 1000 1000 c 5 1" >> $cpio_list

	fi
}

input_file() {
	source="$1"
	if [ -f "$1" ]; then
		# If a regular file is specified, assume it is in
		# gen_init_cpio format
		cat "$1"         >> $cpio_list
	elif [ -d "$1" ]; then
		# If a directory is specified then add all files in it to fs
		dir_filelist "$1"
	else
		echo "  ${prog}: Cannot open '$1'" >&2
		exit 1
	fi
}

prog=$0
cpio_list=$(mktemp ${TMPDIR:-/tmp}/cpiolist.XXXXXX)
output="/dev/stdout"

trap "rm -f $cpio_list" EXIT

while [ $# -gt 0 ]; do
	arg="$1"
	shift
	case "$arg" in
		"-l")	# dependency list - ignored
			shift
			;;
		"-o")	# generate cpio image named $1
			output="$1"
			shift
			;;
		"-u")	# uid mapping - ignored
			shift
			;;
		"-g")	# gid mapping - ignored
			shift
			;;
		*)
			input_file "$arg"
			;;
	esac
done

usr/gen_init_cpio $cpio_list > $output
