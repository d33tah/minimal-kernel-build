#!/bin/sh
# Build the page's v86.wasm: v86 at the pinned commit with v86-det.patch,
# in a pinned build environment, and check its SHA-256.
#
#   site/gen-v86.sh           build in Docker, write site/v86/v86.wasm
#   site/gen-v86.sh --check   offline: site/v86/v86.wasm is that build
#                             (CI's site job runs this on the v86 job's file)
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
#
# Pins: the v86 commit npm 0.5.471 was built from (git checks the commit
# hash), ubuntu:24.04 by digest, clang and lld 18 and nodejs at the versions
# of Ubuntu's apt snapshot of 2026-11-01, Rust 1.90.0 for
# wasm32-unknown-unknown.
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
APT_PACKAGES="clang-18=$LLVM lld-18=$LLVM libllvm18=$LLVM libclang-cpp18=$LLVM libclang1-18=$LLVM libclang-common-18-dev=$LLVM llvm-18-linker-tools=$LLVM nodejs=$NODE libnode109=$NODE python3 make curl"
RUST=1.90.0
SHA256=c4e8e92eab6da3d11c7f62e17230307ac260818030fb83ac0160a22fa8395257

site=$(cd "$(dirname "$0")" && pwd)
out=$site/v86/v86.wasm

check() {
	if echo "$SHA256  $1" | sha256sum -c --quiet; then
		echo "$1: v86 $V86_COMMIT with v86-det.patch, SHA-256 $SHA256"
	else
		echo "$1: SHA-256 $(sha256sum "$1" | cut -d' ' -f1), want $SHA256" >&2
		return 1
	fi
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

mkdir -p "$site/v86"
tar -C "$tmp/v86" --exclude=.git -cf - . |
	docker run --rm -i -e RUSTFLAGS="-C symbol-mangling-version=v0" "$image" \
		sh -c 'mkdir /src && cd /src && tar -xf - && make build/v86.wasm >&2 && cat build/v86.wasm' \
		> "$tmp/v86.wasm"
check "$tmp/v86.wasm"
cp "$tmp/v86.wasm" "$out"
