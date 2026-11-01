#!/bin/sh
# Fetch the pinned v86 runtime into site/v86/ (ignored, never committed).
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
tar -xzf "$tmp/v86.tgz" -C "$tmp" package/build/libv86.js \
	package/build/v86.wasm package/LICENSE
cp "$tmp/package/build/libv86.js" "$tmp/package/build/v86.wasm" "$dir/"
cp "$tmp/package/LICENSE" "$dir/LICENSE.v86"

for f in seabios.bin vgabios.bin COPYING.LESSER; do
	curl -sSfL -o "$dir/$f" \
		"https://raw.githubusercontent.com/copy/v86/$V86_COMMIT/bios/$f"
done

cd "$dir"
sha256sum -c <<SUMS
1b41074f7a5604f528dac6842baf7afccd79d7370b97ea3241944c275b67bec1  libv86.js
2fc5609ae837fcbc77147155de6f1014677ecab5369f5584ebc670e9d56e6510  v86.wasm
73e3f359102e3a9982c35fce98eb7cd08f18303ac7f1ba6ebfbe6cdc1c244d98  seabios.bin
a4bc0d80cc3ca028c73dafa8fee396b8d054ce87ebd8abfbd31b06b437607880  vgabios.bin
3f61ba65e3215b09ef264ba8d777d73c19644309fbb705967634eb5147e500a0  LICENSE.v86
a853c2ffec17057872340eee242ae4d96cbf2b520ae27d903e1b2fef1a5f9d1c  COPYING.LESSER
SUMS
