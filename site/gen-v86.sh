#!/bin/sh
# Build the page's v86.wasm and libv86.js: v86 at the pinned commit with
# v86-det.patch, in a pinned build environment, and check their SHA-256.
#
#   site/gen-v86.sh           build in Docker, write site/v86/v86.wasm
#                             and site/v86/libv86.js
#   site/gen-v86.sh --check   offline: site/v86/ has that build
#                             (CI's site job runs this on the v86 job's files)
#
# The patch (R3, R4) makes v86's JIT repeat a run from a snapshot: hash maps
# with a fixed hasher (iteration order no longer depends on how many maps
# were made before), set_tsc from the clock (a restored TSC is the one
# saved) and an export jit_reset_hidden() that puts the JIT cache, its maps,
# the TLB and the TSC's extrapolation into one canonical state; the page
# calls it after every restore and every snapshot it saves. And (R5) an
# instruction budget, jit_set_budget(on, count): the CPU stops at that
# value of v86's instruction counter exactly (compiled code does not enter
# a basic block that would cross it, the rest is interpreted with the
# counter compiled code reads the clock from), for reverse execution, and
# jit_stats() (instructions interpreted, compiled calls, interpreted runs:
# the page spaces its JIT checkpoints by them).
# M1 (memory events, src/rust/mev.rs and the JS files): the CPU's accesses
# outside RAM are reported to the page (mev_cpu) with their exact position,
# in compiled code too; every other RAM access goes through one small API in
# cpu.js (dma_* for the devices, emu_* for the emulator); a checking mode
# marks the pages the CPU may have written. The JS part is why libv86.js is
# built here too, with Closure Compiler v20210601 (the version v86's
# Makefile fetches) on OpenJDK 21: the same build of the unpatched JS gives
# npm 0.5.471's libv86.js byte for byte, which this script checks every
# time (NPM_LIBV86_SHA256).
#
# Pins: the v86 commit npm 0.5.471 was built from (git checks the commit
# hash), ubuntu:24.04 by digest, clang and lld 18 and nodejs at the versions
# of Ubuntu's apt snapshot of 2026-11-01, OpenJDK 21 at noble's release
# version (the release pocket never changes), Rust 1.90.0 for
# wasm32-unknown-unknown, the Closure Compiler jar by SHA-256.
# RUSTFLAGS -C symbol-mangling-version=v0 gives the function names npm's
# build has, which stepper.js looks up in the name section. The build runs
# with the source copied into the container at /src (paths end up in the
# binary), so the result does not depend on where this runs.
set -eu

V86_COMMIT=94faf3d6b19ecabc53a4010aaa24252fa6a162e2
UBUNTU=ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852
# The toolchain packages at the versions of Ubuntu's apt snapshot of
# 2026-11-01 (clang and lld 18, nodejs): from the archive while it has
# them, else from snapshot.ubuntu.com (which answered 502/503 at times),
# over plain http (apt checks the archive's signatures): when the archive
# itself was out of reach (CI run 43049983587) ca-certificates could not be
# installed first, and https to the snapshot needs it.
APT_SNAPSHOT=20261101T000000Z
LLVM=1:18.1.3-1ubuntu1
NODE=18.19.1+dfsg-6ubuntu5
JDK=21.0.3+9-1ubuntu1
APT_PACKAGES="clang-18=$LLVM lld-18=$LLVM libllvm18=$LLVM libclang-cpp18=$LLVM libclang1-18=$LLVM libclang-common-18-dev=$LLVM llvm-18-linker-tools=$LLVM nodejs=$NODE libnode109=$NODE openjdk-21-jre-headless=$JDK python3 make curl"
RUST=1.90.0
CLOSURE_URL=https://repo1.maven.org/maven2/com/google/javascript/closure-compiler/v20210601/closure-compiler-v20210601.jar
CLOSURE_SHA256=64f161c65a3dba42c9a4f5a79db335ac0e757fcba486cd7928205ac4c258ef08
NPM_LIBV86_SHA256=1b41074f7a5604f528dac6842baf7afccd79d7370b97ea3241944c275b67bec1
SHA256=cdf710949be5d004f0a8da9b559a49ddebdd5fde94d172e2a2cdf365b1a7a6a1
LIBV86_SHA256=adcddf1f99554a9674a16133d44baddf7466234484093e67d6193789fba36c21

site=$(cd "$(dirname "$0")" && pwd)
out=$site/v86

check1() {
	if echo "$2  $1" | sha256sum -c --quiet; then
		echo "$1: v86 $V86_COMMIT with v86-det.patch, SHA-256 $2"
	else
		echo "$1: SHA-256 $(sha256sum "$1" | cut -d' ' -f1), want $2" >&2
		return 1
	fi
}
check() {
	check1 "$1/v86.wasm" "$SHA256" && check1 "$1/libv86.js" "$LIBV86_SHA256"
}

case "${1:-}" in
--check)
	check "$out"
	exit
	;;
"") ;;
*)
	sed -n '2,8s/^# \{0,1\}//p' "$0" >&2
	exit 2
	;;
esac

tmp=$(mktemp -d)
image=mkb-v86-build-$$
trap 'rm -rf "$tmp"; docker rmi -f "$image" >/dev/null 2>&1 || true' EXIT

git -c init.defaultBranch=main init -q "$tmp/v86"
git -C "$tmp/v86" fetch -q --depth 1 https://github.com/copy/v86 "$V86_COMMIT"
git -C "$tmp/v86" -c advice.detachedHead=false checkout -q FETCH_HEAD
test "$(git -C "$tmp/v86" rev-parse HEAD)" = "$V86_COMMIT"
curl -sSfL --retry 5 -o "$tmp/compiler.jar" "$CLOSURE_URL"
echo "$CLOSURE_SHA256  $tmp/compiler.jar" | sha256sum -c --quiet
mkdir "$tmp/v86/closure-compiler"
cp "$tmp/compiler.jar" "$tmp/v86/closure-compiler/"
# the unpatched tree: its libv86.js must be npm's
cp -r "$tmp/v86" "$tmp/upstream"
git -C "$tmp/v86" apply "$site/v86-det.patch"

cat > "$tmp/Dockerfile" <<DOCKERFILE
FROM $UBUNTU
ENV DEBIAN_FRONTEND=noninteractive
RUN { apt-get -o Acquire::Retries=5 update \\
      && apt-get -o Acquire::Retries=5 install -y --no-install-recommends ca-certificates $APT_PACKAGES; } \\
    || { sed -i 's#http://\\(archive\\|security\\)\\.ubuntu\\.com/ubuntu/\\?#http://snapshot.ubuntu.com/ubuntu/$APT_SNAPSHOT/#' /etc/apt/sources.list.d/ubuntu.sources \\
         && apt-get -o Acquire::Retries=5 update \\
         && apt-get -o Acquire::Retries=5 install -y --no-install-recommends ca-certificates $APT_PACKAGES; } \\
    && rm -rf /var/lib/apt/lists/* && ln -s /usr/bin/clang-18 /usr/local/bin/clang
ENV RUSTUP_HOME=/opt/rustup CARGO_HOME=/opt/cargo PATH=/opt/cargo/bin:\$PATH
RUN curl -sSf --retry 5 https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain $RUST --target wasm32-unknown-unknown
DOCKERFILE
# snapshot.ubuntu.com answers 502/503 at times (CI run 43010520643): try
# the image three times, five minutes apart.
for try in 1 2 3; do
	docker build --progress=plain -t "$image" "$tmp" >&2 && break
	test $try = 3 && exit 1
	echo "gen-v86.sh: the image build failed (try $try of 3), again in 5 minutes" >&2
	sleep 300
done

mkdir -p "$out" "$tmp/out"
# The same container run for both trees, the source at /src each time
# (paths end up in v86.wasm).
build() {
	tar -C "$1" --exclude=.git -cf - . |
		docker run --rm -i -e RUSTFLAGS="-C symbol-mangling-version=v0" "$image" \
			sh -c "mkdir /src && cd /src && tar -xf - && make $2 >&2 && tar -cf - $2"
}
build "$tmp/upstream" build/libv86.js | tar -xO > "$tmp/out/npm-libv86.js"
check1 "$tmp/out/npm-libv86.js" "$NPM_LIBV86_SHA256"
build "$tmp/v86" "build/v86.wasm build/libv86.js" | tar -C "$tmp/out" -xf -
check "$tmp/out/build"
cp "$tmp/out/build/v86.wasm" "$tmp/out/build/libv86.js" "$out/"
