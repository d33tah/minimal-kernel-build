#!/bin/sh
# The build kits of the kernels the page rebuilds (plan E3; gen-kit.py),
# from the kernels job's -g trees, then the kits built again under Node
# with clang.wasm (test_kit.mjs: CI's bytes from CI's objects, and from
# the sources). Run from the repository root:
#
#   site/gen-kits.sh G HELLO_OS O HOSTWASM KITS
#
# G: the -g trees (hello, hello-os, minified, minified-os); HELLO_OS and
# O/minified: the plain hello-os and -O0 minified trees (the page boots
# their bzImages, and hello/'s and minified/'s); HOSTWASM:
# gen-host-tools.py's output (hello/, hello-os/, minified/, minified-os/,
# nasm/); KITS: the output.
set -eu
g=$1 hello_os=$2 o=$3 m=$4 k=$5
rm -rf "$k"
kit() {
	python3 site/gen-kit.py "$@"
}
hello() { # name, plain tree, make args
	n=$1 p=$2
	shift 2
	kit --name "$n" --tree "$g/$n" --out "$k" --sources-from hello \
		--clean boot/head.o --clean boot/hello.o --clean boot/high --clean boot/low --clean bzImage --clean bzImage.low \
		--tool tools/build="$m/$n/build.wasm" --module nasm="$m/nasm/nasm.wasm" \
		--product bzImage --product boot/high/kernel.elf --shipped bzImage="$p/bzImage" -- make LLVM=1 "$@"
}
hello hello hello OPT=-O0 "CC=clang -g"
hello hello-os "$hello_os" "CC=clang -g"
minified() { # name, -g tree, module dir, plain tree, make args
	n=$1 t=$2 md=$3 p=$4
	shift 4
	kit --name "$n" --tree "$g/$t" --out "$k" --sources-from minified --kbuild --touch-objects \
		--tool arch/x86/boot/tools/build="$m/$md/build.wasm" --tool arch/x86/boot/compressed/mkpiggy="$m/$md/mkpiggy.wasm" \
		--extra-input usr/initramfs_data.o=usr/initramfs_data.cpio \
		--gen-nasm elo/init=elo/init.nasm --initramfs usr/initramfs_data.cpio=elo --module nasm="$m/nasm/nasm.wasm" \
		--product vmlinux --product arch/x86/boot/bzImage --shipped arch/x86/boot/bzImage="$p/arch/x86/boot/bzImage" \
		-- make LLVM=1 "$@"
}
minified minified-linux minified minified "$o/minified" "KCFLAGS=-g -O0"
minified minified-linux-os minified-os minified-os minified KCFLAGS=-g
python3 site/gen-kit.py --index "$k"
# nasm.wasm (hello's boot code, the minified Linux's /init) is in blobs/: its licence next to it.
cp "$m/nasm/LICENSE.nasm" "$k/"

node site/test_kit.mjs --unit
node site/test_kit.mjs --build site/clang/clang.wasm "$k" hello hello-os minified-linux minified-linux-os
node site/test_kit.mjs --build site/clang/clang.wasm "$k" hello hello-os minified-linux minified-linux-os --compile all
