#!/bin/sh
# Fetch the pinned v86 runtime into site/v86/ (ignored, never committed):
# the BIOSes and v86's licence. v86.wasm and libv86.js are built from the
# same commit with v86-det.patch by gen-v86.sh, not taken from npm (its
# build of the unpatched source is npm's libv86.js byte for byte).
# Pins and licences: see site/THIRD_PARTY.md. Every file is checked
# against the SHA-256 recorded below, so a moved or altered upstream file
# fails the build instead of being deployed.
set -eu

V86_NPM_VERSION=0.5.471
V86_COMMIT=94faf3d6b19ecabc53a4010aaa24252fa6a162e2

dir=$(dirname "$0")/v86
mkdir -p "$dir"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

curl -sSfL -o "$tmp/v86.tgz" \
	"https://registry.npmjs.org/v86/-/v86-$V86_NPM_VERSION.tgz"
tar -xzf "$tmp/v86.tgz" -C "$tmp" package/LICENSE
cp "$tmp/package/LICENSE" "$dir/LICENSE.v86"

for f in seabios.bin vgabios.bin COPYING.LESSER; do
	curl -sSfL -o "$dir/$f" \
		"https://raw.githubusercontent.com/copy/v86/$V86_COMMIT/bios/$f"
done

cd "$dir"
sha256sum -c <<SUMS
73e3f359102e3a9982c35fce98eb7cd08f18303ac7f1ba6ebfbe6cdc1c244d98  seabios.bin
a4bc0d80cc3ca028c73dafa8fee396b8d054ce87ebd8abfbd31b06b437607880  vgabios.bin
3f61ba65e3215b09ef264ba8d777d73c19644309fbb705967634eb5147e500a0  LICENSE.v86
a853c2ffec17057872340eee242ae4d96cbf2b520ae27d903e1b2fef1a5f9d1c  COPYING.LESSER
SUMS

# iced-x86 (MIT): the in-browser x86 disassembler for listings without
# disasm.json (livedis.js). npm build 1.21.0 (2024-01-20), into site/iced/.
ICED_VERSION=1.21.0
idir=$(dirname "$0")/iced
cd - >/dev/null
mkdir -p "$idir"
curl -sSfL -o "$tmp/iced.tgz" "https://registry.npmjs.org/iced-x86/-/iced-x86-$ICED_VERSION.tgz"
tar -xzf "$tmp/iced.tgz" -C "$tmp" package/iced_x86.js package/iced_x86_bg.wasm package/LICENSE.txt
cp "$tmp/package/iced_x86.js" "$tmp/package/iced_x86_bg.wasm" "$idir/"
cp "$tmp/package/LICENSE.txt" "$idir/LICENSE.iced"
cd "$idir"
sha256sum -c <<SUMS
d60239c35c1d4d4407133c315e2b7301c0a7e3826edfa8650414f8fd6684d3b0  iced_x86.js
e6b572689d3296b15d58d7944f37745435a6aa914df610e0fd42ce0f964033a1  iced_x86_bg.wasm
93a47598376fd3f276b631e75149340087153c6ff1635a53c5790249e9124e2d  LICENSE.iced
SUMS
