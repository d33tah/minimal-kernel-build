#!/bin/sh
# Builds the initramfs for the Debian 6.1 686 sample the way Debian does
# (initramfs-tools' mkinitramfs, gzip), inside an i386 Debian bookworm
# container, from the same pinned kernel package:
#
#   docker run --rm --platform linux/386 -v "$PWD:/w" -w /w debian:bookworm-slim \
#     sh site/gen-debian-initrd.sh debian-debs/linux-image-6.1.0-50-686_6.1.176-1_i386.deb out/initrd.img
#
# Packages come from the same snapshot.debian.org date as the kernel, so
# the result depends only on the snapshot and this script.
#
# MODULES=list with an empty list: no kernel modules (v86 has no disk
# here). The result is 5.4 MB and boots to the (initramfs) shell with 96
# or 128 MB of RAM; Debian's default MODULES=most gives 25.7 MB, which the
# kernel cannot unpack in 128 MB ("No working init found"). MODULES=dep
# would pick the modules of the machine that builds it.
set -eu
deb=$1
out=$2
snap=${DEBIAN_SNAPSHOT:-20260704T211317Z}
ver=6.1.0-50-686
rm -f /etc/apt/sources.list.d/*
echo "deb http://snapshot.debian.org/archive/debian/$snap bookworm main" > /etc/apt/sources.list
apt-get -q -o Acquire::Check-Valid-Until=false -o Acquire::Retries=3 update
mkdir -p /etc/initramfs-tools/conf.d
echo 'COMPRESS=gzip' > /etc/initramfs-tools/conf.d/compress
DEBIAN_FRONTEND=noninteractive apt-get -q -y --no-install-recommends install \
	initramfs-tools busybox klibc-utils kmod udev "./$deb" > /dev/null
sed -i 's/^MODULES=.*/MODULES=list/' /etc/initramfs-tools/initramfs.conf
mkinitramfs -o "$out" "$ver"
# Which Debian packages its files come from (binary package, version,
# source package), written next to it as packages.txt for THIRD_PARTY.md:
# each regular file of the unpacked initramfs whose content is identical
# to an installed file (also under its pre-merged-/usr path, which is
# what dpkg records) is attributed to that file's package.
x=$(mktemp -d)
unmkinitramfs "$out" "$x"
[ -d "$x/main" ] && x="$x/main"
(cd "$x" && find . -type f) | sed 's,^\.,,' | while read -r f; do
	for h in "$f" "${f#/usr}"; do
		if [ -f "$h" ] && cmp -s "$x$f" "$h" && p=$(dpkg -S "$h" 2>/dev/null); then
			echo "$p" | sed 's/: .*//' | tr ',' '\n' | tr -d ' '
			break
		fi
	done
	# Copied under other paths: /init and scripts/ from
	# /usr/share/initramfs-tools, klibc's tools from /usr/lib/klibc/bin.
	[ "$f" = /init ] && echo initramfs-tools-core && echo klibc-utils
done | sed 's/:i386$//' | grep -v '^$' | sort -u | while read -r pkg; do
	dpkg-query -W -f '${Package} ${Version} ${source:Package}\n' "$pkg"
done > "$(dirname "$out")/packages.txt"
cat "$(dirname "$out")/packages.txt"
ls -l "$out"
