#!/usr/bin/env bash
# The source tree Debian built linux-image-6.1.0-50-686 from, for the page's
# C source view (gen-src.js picks the files its DWARF names):
#
#   site/gen-debian-source.sh ORIG_TAR_XZ DEBIAN_TAR_XZ OUTDIR
#
# Unpacks the upstream tarball (linux_<ver>.orig.tar.xz), applies the quilt
# series of the Debian tarball (debian/patches/series: the 686 flavour
# builds the featureset "none", which is that series alone), then
# regenerates the two headers the build generates and the DWARF refers to
# with the kernel's own scripts: arch/x86/include/generated/asm/syscalls_32.h
# (scripts/syscalltbl.sh) and security/selinux/flask.h (genheaders, a host
# program). The callers check the tarballs' SHA-256 (pinned in the .dsc).
set -euo pipefail

if [ $# -ne 3 ]; then
	sed -n '2,/^set /p' "$0" | sed '$d' >&2
	exit 2
fi
orig=$(realpath "$1")
debian=$(realpath "$2")
out=$3

rm -rf "$out"
mkdir -p "$out"
tar -xJf "$orig" -C "$out" --strip-components=1
tar -xJf "$debian" -C "$out" debian/patches

# Every series line is a patch path, applied with -p1 (no options in this series).
n=0
while read -r p; do
	case "$p" in '' | '#'*) continue ;; esac
	patch -d "$out" -p1 -s -f --no-backup-if-mismatch < "$out/debian/patches/$p"
	n=$((n + 1))
done < "$out/debian/patches/series"
echo "applied $n patches of debian/patches/series"

gen=$out/arch/x86/include/generated/asm
mkdir -p "$gen"
(cd "$out" && sh scripts/syscalltbl.sh --abis i386 arch/x86/entry/syscalls/syscall_32.tbl arch/x86/include/generated/asm/syscalls_32.h)
cc -I"$out/include/uapi" -I"$out/include" -I"$out/security/selinux/include" \
	-o "$out/scripts/selinux/genheaders/genheaders" "$out/scripts/selinux/genheaders/genheaders.c"
"$out/scripts/selinux/genheaders/genheaders" "$out/security/selinux/flask.h" "$out/security/selinux/av_permissions.h"
rm -rf "$out/debian"
echo "regenerated $gen/syscalls_32.h and security/selinux/flask.h"
