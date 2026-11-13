#!/bin/sh
# Build the native toolchain CI builds the kernels with: clang 22.1.0,
# ld.lld and the llvm tools (x86 backend only, for x86_64 Linux hosts) from
# the same source as clang.wasm (gen-clang-wasm.sh), so an object the page
# compiles with clang.wasm can equal CI's object byte for byte.
#
#   site/gen-clang-native.sh           build in Docker, write site/clang-native/
#   site/gen-clang-native.sh --check   offline: site/clang-native/ is that build
#
# site/clang-native/clang-native.tar unpacks to bin/ (the multicall binary
# `llvm` and its links clang, ld.lld, llvm-ar, llvm-nm, llvm-objcopy,
# llvm-objdump, llvm-readelf, llvm-strip, ...) and lib/clang/22/include
# (clang's own headers). CI unpacks it to /opt/clang-22 and puts its bin/
# first in PATH.
#
# Pins: those of gen-clang-wasm.sh (the YoWASP llvm-project commit,
# debian:trixie by digest, cmake, ninja, g++ and python3 at fixed Debian
# versions; --check fails when the two scripts differ in them). The recipe
# is gen-clang-wasm.sh's for the same tool set (llvm-driver, X86 only, no
# zlib/zstd/libxml2) as a native Release build by g++-14, stripped. The tree
# is copied into the container at /w (no .git, so no revision in the
# version string) and the tar is written there with fixed owners, order
# and times, so the result does not depend on where this runs.
set -eu

LLVM_REPO=https://github.com/YoWASP/llvm-project
LLVM_COMMIT=97196c8eeb1d495fa43bb8af2fb26af5ef5b89fb
DEBIAN=debian:trixie@sha256:9c6e381421d0f48d1b8133e573f33c185ee16cbf505eda9c4a8a4791b25ea8a6
APT_SNAPSHOT=20261113T000000Z
APT_PACKAGES="cmake=3.31.6-2 cmake-data=3.31.6-2 ninja-build=1.12.1-1 g++-14=14.2.0-19 python3=3.13.5-1"
SHA256=7483ebcabbba9b8fada85b5a07a88cfa0db2017d5a54ac630f1835ca446b0a86

site=$(cd "$(dirname "$0")" && pwd)
dir=$site/clang-native

# The pins above must be gen-clang-wasm.sh's: same source, same build
# environment.
same_pins() {
	for v in LLVM_REPO LLVM_COMMIT DEBIAN APT_SNAPSHOT APT_PACKAGES; do
		a=$(/usr/bin/grep "^$v=" "$0")
		b=$(/usr/bin/grep "^$v=" "$site/gen-clang-wasm.sh")
		if [ "$a" != "$b" ]; then
			echo "gen-clang-native.sh: $v differs from gen-clang-wasm.sh: $a / $b" >&2
			return 1
		fi
	done
}

check() {
	same_pins || return 1
	if echo "$SHA256  $1/clang-native.tar" | sha256sum -c --quiet; then
		echo "$1: native clang/lld 22.1.0 x86 ($LLVM_COMMIT), SHA-256 $SHA256"
	else
		sha256sum "$1/clang-native.tar" >&2
		return 1
	fi
}

case "${1:-}" in
--check)
	check "$dir"
	exit
	;;
"") ;;
*)
	sed -n '2,8s/^# \{0,1\}//p' "$0" >&2
	exit 2
	;;
esac

same_pins
tmp=$(mktemp -d)
image=mkb-clang-native-build-$$
trap 'rm -rf "$tmp"; docker rmi -f "$image" >/dev/null 2>&1 || true' EXIT

mkdir "$tmp/w" "$tmp/src" "$tmp/out"
git -c init.defaultBranch=main init -q "$tmp/src"
git -C "$tmp/src" fetch -q --depth 1 "$LLVM_REPO" "$LLVM_COMMIT"
git -C "$tmp/src" -c advice.detachedHead=false checkout -q FETCH_HEAD
test "$(git -C "$tmp/src" rev-parse HEAD)" = "$LLVM_COMMIT"
tar -C "$tmp/src" --exclude=.git -cf - cmake llvm clang lld third-party libunwind/include |
	{ mkdir "$tmp/w/llvm" && tar -C "$tmp/w/llvm" -xf -; }
rm -rf "$tmp/src"

cat > "$tmp/w/build.sh" <<'BUILD'
#!/bin/sh
set -eu
cd /w
# gen-clang-wasm.sh's tool list: everything but the llvm-driver and the
# tools in it is off.
OFF=
for t in BUGPOINT BUGPOINT_PASSES DSYMUTIL DXIL_DIS GOLD LLC LLI LLVM_AS LLVM_BCANALYZER \
  LLVM_CAT LLVM_CFI_VERIFY LLVM_CONFIG LLVM_COV LLVM_CVTRES LLVM_CXXDUMP LLVM_CXXFILT \
  LLVM_CXXMAP LLVM_C_TEST LLVM_DEBUGINFOD LLVM_DEBUGINFOD_FIND LLVM_DEBUGINFO_ANALYZER \
  LLVM_DIFF LLVM_DIS LLVM_DWARFDUMP LLVM_DWARFUTIL LLVM_DWP LLVM_EXEGESIS LLVM_EXTRACT \
  LLVM_GSYMUTIL LLVM_IFS LLVM_JITLINK LLVM_JITLISTENER LLVM_LIBTOOL_DARWIN LLVM_LINK \
  LLVM_LIPO LLVM_LTO2 LLVM_LTO LLVM_MCA LLVM_MC LLVM_ML LLVM_MODEXTRACT LLVM_MT \
  LLVM_OPT_REPORT LLVM_PDBUTIL LLVM_PROFDATA LLVM_PROFGEN LLVM_RC LLVM_READTAPI \
  LLVM_REDUCE LLVM_REMARKUTIL LLVM_RTDYLD LLVM_SHLIB LLVM_SIM LLVM_SIZE LLVM_SPLIT \
  LLVM_STRESS LLVM_STRINGS LLVM_SYMBOLIZER LLVM_TLI_CHECKER LLVM_UNDNAME LLVM_XRAY LTO \
  OBJ2YAML OPT OPT_VIEWER REDUCE_CHUNK_LIST REMARKS_SHLIB SANCOV SANSTATS SPIRV_TOOLS \
  VERIFY_USELISTORDER XCODE_TOOLCHAIN YAML2OBJ; do
  OFF="$OFF -DLLVM_TOOL_${t}_BUILD=OFF"
done
cmake -G Ninja -B native -S llvm/llvm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 -DCMAKE_EXE_LINKER_FLAGS=-Wl,--strip-all \
  -DLLVM_ENABLE_ASSERTIONS=OFF -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF \
  $OFF -DLLVM_TOOL_LLVM_DRIVER_BUILD=ON \
  -DLLVM_TOOL_LLVM_AR_BUILD=ON -DLLVM_TOOL_LLVM_NM_BUILD=ON -DLLVM_TOOL_LLVM_OBJCOPY_BUILD=ON \
  -DLLVM_TOOL_LLVM_OBJDUMP_BUILD=ON -DLLVM_TOOL_LLVM_READOBJ_BUILD=ON \
  -DLLVM_ENABLE_PROJECTS="clang;lld" -DCLANG_ENABLE_ARCMT=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
  -DCLANG_INCLUDE_TESTS=OFF -DCLANG_BUILD_TOOLS=OFF -DCLANG_TOOL_CLANG_SCAN_DEPS_BUILD=OFF \
  -DCLANG_TOOL_CLANG_INSTALLAPI_BUILD=OFF -DCLANG_BUILD_EXAMPLES=OFF -DCLANG_INCLUDE_DOCS=OFF \
  -DCLANG_LINKS_TO_CREATE=clang -DLLD_BUILD_TOOLS=OFF \
  -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
  -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_LIBPFM=OFF \
  -DLLVM_PARALLEL_LINK_JOBS=1 >&2
start=$(date +%s)
ninja -C native ${NINJA_JOBS:+-j$NINJA_JOBS} llvm-driver clang-resource-headers >&2
echo "native llvm build: $(( $(date +%s) - start )) s" >&2
mkdir -p /w/out/bin /w/out/lib/clang
cp native/bin/llvm /w/out/bin/llvm
for t in clang clang++ clang-cpp ld.lld lld llvm-ar llvm-ranlib llvm-nm llvm-objcopy \
  llvm-strip llvm-objdump llvm-readobj llvm-readelf; do
  ln -s llvm "/w/out/bin/$t"
done
cp -a native/lib/clang/22 /w/out/lib/clang/22
for t in clang ld.lld llvm-ar llvm-ranlib llvm-nm llvm-objcopy llvm-strip llvm-objdump \
  llvm-readobj llvm-readelf; do
  /w/out/bin/$t --version >&2
done
test "$(/w/out/bin/clang -print-file-name=include)" = /w/out/lib/clang/22/include
tar -C /w/out --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
  --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime -cf - bin lib
BUILD

cat > "$tmp/Dockerfile" <<DOCKERFILE
FROM $DEBIAN
ENV DEBIAN_FRONTEND=noninteractive
RUN { apt-get -o Acquire::Retries=5 update \\
      && apt-get -o Acquire::Retries=5 install -y --no-install-recommends $APT_PACKAGES; } \\
    || { sed -i 's#http://deb.debian.org/debian-security#http://snapshot.debian.org/archive/debian-security/$APT_SNAPSHOT#; s#http://deb.debian.org/debian#http://snapshot.debian.org/archive/debian/$APT_SNAPSHOT#' /etc/apt/sources.list.d/debian.sources \\
         && apt-get -o Acquire::Retries=5 -o Acquire::Check-Valid-Until=false update \\
         && apt-get -o Acquire::Retries=5 install -y --no-install-recommends $APT_PACKAGES; } \\
    && rm -rf /var/lib/apt/lists/*
DOCKERFILE
for try in 1 2 3; do
	docker build --progress=plain -t "$image" "$tmp" >&2 && break
	test $try = 3 && exit 1
	echo "gen-clang-native.sh: the image build failed (try $try of 3), again in 5 minutes" >&2
	sleep 300
done

tar -C "$tmp/w" -cf - . |
	docker run --rm -i ${NINJA_JOBS:+-e NINJA_JOBS=$NINJA_JOBS} "$image" \
		sh -c 'mkdir /w && cd /w && tar -xf - && sh build.sh' > "$tmp/out/clang-native.tar"
rm -rf "$tmp/w"
mkdir -p "$dir"
if ! check "$tmp/out"; then
	# Kept for a look (or a new pin), never as the build.
	cp "$tmp/out/clang-native.tar" "$dir/clang-native.tar.rejected"
	exit 1
fi
rm -f "$dir/clang-native.tar"
cp "$tmp/out/clang-native.tar" "$dir/"
