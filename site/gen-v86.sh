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
# calls it after every restore and every snapshot it saves.
#
# Pins: the v86 commit npm 0.5.471 was built from (git checks the commit
# hash), ubuntu:24.04 by digest, Ubuntu's apt snapshot of 2026-11-01 (clang
# and lld 18, nodejs, python3), Rust 1.90.0 for wasm32-unknown-unknown.
# RUSTFLAGS -C symbol-mangling-version=v0 gives the function names npm's
# build has, which stepper.js looks up in the name section. The build runs
# with the source copied into the container at /src (paths end up in the
# binary), so the result does not depend on where this runs.
set -eu

V86_COMMIT=94faf3d6b19ecabc53a4010aaa24252fa6a162e2
UBUNTU=ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852
APT_SNAPSHOT=20261101T000000Z
RUST=1.90.0
SHA256=a644af5f6759dfb5b02f60bd802988bad946d092f0ee6b992702188f1a9170a8

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
RUN apt-get -o Acquire::Retries=5 update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates \\
    && apt-get -o Acquire::Retries=5 update --snapshot $APT_SNAPSHOT \\
    && DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=5 install --snapshot $APT_SNAPSHOT -y --no-install-recommends \\
       clang-18 lld-18 python3 nodejs make curl \\
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
