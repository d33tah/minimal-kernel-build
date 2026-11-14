#!/bin/sh
# Build the in-browser toolchain clang.wasm: clang 22.1.0, ld.lld and the
# llvm tools with the x86 backend only, for wasm32-wasip1, in a pinned
# build environment, and check its SHA-256 and its licence files.
#
#   site/gen-clang-wasm.sh           build in Docker, write site/clang/
#   site/gen-clang-wasm.sh --check   offline: site/clang/ is that build
#                                    (CI checks the cached or new build)
#
# clang.wasm is LLVM's multicall binary (llvm-driver): argv[0] picks the
# tool (clang, ld.lld, llvm-ar, llvm-nm, llvm-objcopy, llvm-objdump,
# llvm-readobj, llvm-strip). It compiles i386/x86_64 objects only (no wasm
# backend); objects and links are byte-identical to a native clang/lld
# built from the same commit (E0 spike, PROGRESS in the plan).
#
# Pins: YoWASP's llvm-project fork at the commit of its 22.1.0 release
# (LLVM 22.1.0 with YoWASP's WASI patches; git checks the commit hash),
# wasi-sdk 29 by SHA-256 (its wasi-libc and libc++ are linked in), and
# debian:trixie by digest with cmake, ninja, g++ and python3 at the
# versions of Debian's archive of 2026-11-13 (from the archive while it
# has them, else from snapshot.debian.org). The recipe is YoWASP's
# build.sh (MinSizeRel, no threads, 8 MiB stack, 4 GiB max memory) with
# LLVM_TARGETS_TO_BUILD=X86 and without LTO, and one patch (below: a
# WASI file mode call). Native llvm-tblgen and
# clang-tblgen are built first in the same container. The tree is copied
# into the container at /w (no .git, so no revision in the version
# string), so the result does not depend on where this runs.
set -eu

LLVM_REPO=https://github.com/YoWASP/llvm-project
LLVM_COMMIT=97196c8eeb1d495fa43bb8af2fb26af5ef5b89fb
WASI_SDK=wasi-sdk-29.0-x86_64-linux
WASI_SDK_URL=https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-29/$WASI_SDK.tar.gz
WASI_SDK_SHA256=87d1d1a2879d139cdc624b968efad3d4a97b8078cdff95e63ac88ecafd1a0171
# wasi-sdk 29's wasi-libc (its VERSION file: wasi-libc ac020b86fd44).
WASI_LIBC_COMMIT=ac020b86fd44bafe60aa4fa12f407d16e3731329
DEBIAN=debian:trixie@sha256:9c6e381421d0f48d1b8133e573f33c185ee16cbf505eda9c4a8a4791b25ea8a6
APT_SNAPSHOT=20261113T000000Z
APT_PACKAGES="cmake=3.31.6-2 cmake-data=3.31.6-2 ninja-build=1.12.1-1 g++-14=14.2.0-19 python3=3.13.5-1"

site=$(cd "$(dirname "$0")" && pwd)
dir=$site/clang

# Every file of site/clang/: clang.wasm and the licences of what is in it.
# clang.wasm: this script's build on d-claude with the patch below (E0d;
# without it the E0 spike, d-claude and CI all gave 8b619cda...b755827).
sums() {
	cat <<SUMS
4f4bd4b16f41b0d4263c2ed33442311315837eab0d08d81be1c300a9fe9255fe  clang.wasm
6a94bedb8b707ed97f6e310d0d015ab14e0683ffa0a612b02958581b9cc9fc0e  LICENSE.blake3
ebcd9bbf783a73d05c53ba4d586b8d5813dcdf3bbec50265860ccc885e606f47  LICENSE.clang
1a8f1058753f1ba890de984e48f0242a3a5c29a6a8f2ed9fd813f36985387e8d  LICENSE.compiler-rt
539dd7aed86e8a4f12cbdd0e6c50c189c7d74847e4fecc64ce2c6ee3a01da38b  LICENSE.libcxx
e2b35be49f7284a45b7baca8fc7b3ab7440e7902392b2528a457816b5bb2a15c  LICENSE.libcxxabi
f7891568956e34643eb6a0db1462db30820d40d7266e2a78063f2fe233ece5a0  LICENSE.lld
8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee  LICENSE.llvm
59a0d2fd71bea59e37b3284f962d2a5a7a0cb63290df174801a18212695b2130  LICENSE.md5
0424e57d4303164dc59a8509c20dae0518b853692e5c2b0e98b11816fdbc97c7  LICENSE.regex
75d279526d16d52e1d71bb89cec8a5b43b3642d5ea9c75f24538f6e1338d3e5a  LICENSE.unicode
2355f8ab91787f152998ecaef1744356a22b98ac80d6babaf95e3de99d7c7d25  LICENSE.unicode-convertutf
2711a8b5a5cdfef0e639f96c1aca12ae23d7d64a02d0507f1bdf14d2b27bbc3a  LICENSE.wasi-libc
a60eea817514531668d7e00765731449fe14d059d3249e0bc93b36de45f759f2  LICENSE.wasi-libc-apache
268872b9816f90fd8e85db5a28d33f8150ebb8dd016653fb39ef1f94f2686bc5  LICENSE.wasi-libc-apache-llvm
c8b789cf5a746611e6300a0cc7750dbf92b61912a709d04e639245f7290656d0  LICENSE.wasi-libc-cloudlibc
23f18e03dc49df91622fe2a76176497404e46ced8a715d9d2b67a7446571cca3  LICENSE.wasi-libc-mit
f9bc4423732350eb0b3f7ed7e91d530298476f8fec0c6c427a1c04ade22655af  LICENSE.wasi-libc-musl
4eca3924b5c7daab59e93307726c44b21de2de1b0df21acf0f6a62f48f149891  LICENSE.xxhash
SUMS
}

check() {
	if (cd "$1" && sums | sha256sum -c --quiet); then
		echo "$1: clang/lld 22.1.0 x86 for wasm32-wasip1 ($LLVM_COMMIT), SHA-256 $(sums | sed -n 's/  clang.wasm$//p')"
	else
		(cd "$1" && sha256sum clang.wasm LICENSE.*) >&2
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

tmp=$(mktemp -d)
image=mkb-clang-wasm-build-$$
trap 'rm -rf "$tmp"; docker rmi -f "$image" >/dev/null 2>&1 || true' EXIT

mkdir "$tmp/w" "$tmp/src" "$tmp/out"
git -c init.defaultBranch=main init -q "$tmp/src"
git -C "$tmp/src" fetch -q --depth 1 "$LLVM_REPO" "$LLVM_COMMIT"
git -C "$tmp/src" -c advice.detachedHead=false checkout -q FETCH_HEAD
test "$(git -C "$tmp/src" rev-parse HEAD)" = "$LLVM_COMMIT"
# One change to the pinned source. llvm-objcopy and llvm-strip write their
# output, then set its mode with fchmod, which wasi-libc answers with
# ENOSYS without asking the WASI host (WASI preview1 has no file modes):
# the run failed with "Function not implemented" after writing the file
# (E0c). On WASI setPermissions(FD) does nothing and succeeds.
git -C "$tmp/src" apply <<'PATCH'
--- a/llvm/lib/Support/Unix/Path.inc
+++ b/llvm/lib/Support/Unix/Path.inc
@@ -830,6 +830,12 @@ std::error_code setPermissions(const Twine &Path, perms Permissions) {
 }
 
 std::error_code setPermissions(int FD, perms Permissions) {
+#if defined(__wasi__)
+  // WASI preview1 has no file modes; wasi-libc's fchmod fails with ENOSYS.
+  (void)FD;
+  (void)Permissions;
+  return std::error_code();
+#endif
   if (::fchmod(FD, Permissions))
     return errnoAsErrorCode();
   return std::error_code();
PATCH
curl -sSfL --retry 5 -o "$tmp/wasi-sdk.tgz" "$WASI_SDK_URL"
echo "$WASI_SDK_SHA256  $tmp/wasi-sdk.tgz" | sha256sum -c --quiet

# Licences: LLVM's (clang, lld and the llvm libraries; libc++, libc++abi
# and compiler-rt's builtins come from wasi-sdk under the same licence),
# the third-party code in LLVM's Support library (BLAKE3, regex; xxhash,
# MD5 and the Unicode code have their terms only in the file's header) and
# wasi-libc's (the libc linked in).
while read -r f n; do
	cp "$tmp/src/$f" "$tmp/out/LICENSE.$n"
done <<LIST
llvm/LICENSE.TXT llvm
clang/LICENSE.TXT clang
lld/LICENSE.TXT lld
libcxx/LICENSE.TXT libcxx
libcxxabi/LICENSE.TXT libcxxabi
compiler-rt/LICENSE.TXT compiler-rt
llvm/lib/Support/BLAKE3/LICENSE blake3
llvm/lib/Support/COPYRIGHT.regex regex
LIST
sup=$tmp/src/llvm/lib/Support
sed -n '1,/\*\//p' "$sup/xxhash.cpp" > "$tmp/out/LICENSE.xxhash"
sed -n '1,/\*\//p' "$sup/MD5.cpp" > "$tmp/out/LICENSE.md5"
sed -n '8,/\*\//p' "$sup/ConvertUTF.cpp" > "$tmp/out/LICENSE.unicode-convertutf"
sed -n '16,/\*\//p' "$sup/UnicodeNameToCodepointGenerated.cpp" > "$tmp/out/LICENSE.unicode"
while read -r f n; do
	curl -sSfL --retry 5 -o "$tmp/out/LICENSE.$n" \
		"https://raw.githubusercontent.com/WebAssembly/wasi-libc/$WASI_LIBC_COMMIT/$f"
done <<LIST
LICENSE wasi-libc
LICENSE-APACHE-LLVM wasi-libc-apache-llvm
LICENSE-APACHE wasi-libc-apache
LICENSE-MIT wasi-libc-mit
libc-bottom-half/cloudlibc/LICENSE wasi-libc-cloudlibc
libc-top-half/musl/COPYRIGHT wasi-libc-musl
LIST

tar -C "$tmp/src" --exclude=.git -cf - cmake llvm clang lld third-party libunwind/include |
	{ mkdir "$tmp/w/llvm" && tar -C "$tmp/w/llvm" -xf -; }
tar -C "$tmp/w" -xzf "$tmp/wasi-sdk.tgz"
rm -rf "$tmp/src" "$tmp/wasi-sdk.tgz"

cat > "$tmp/w/build.sh" <<'BUILD'
#!/bin/sh
set -eu
cd /w
W=/w/WASI_SDK
# Native tblgen tools (they write C++ sources; the compiler does not matter).
cmake -G Ninja -B native -S llvm/llvm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF \
  -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF >&2
ninja -C native llvm-min-tblgen llvm-tblgen clang-tblgen >&2
CF="--sysroot $W/share/wasi-sysroot -mcpu=lime1 -D_WASI_EMULATED_MMAN"
LF="--sysroot $W/share/wasi-sysroot -lwasi-emulated-mman -Wl,--max-memory=4294967296 -Wl,-z,stack-size=8388608,--stack-first -Wl,--strip-all"
cat > tc.cmake <<END
set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_COMPILER $W/bin/clang)
set(CMAKE_C_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_CXX_COMPILER $W/bin/clang++)
set(CMAKE_CXX_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_LINKER $W/bin/wasm-ld)
set(CMAKE_AR $W/bin/ar)
set(CMAKE_RANLIB $W/bin/ranlib)
set(CMAKE_C_FLAGS "$CF")
set(CMAKE_CXX_FLAGS "$CF")
set(CMAKE_EXE_LINKER_FLAGS "$LF")
END
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
cmake -G Ninja -B wasm -S llvm/llvm -DCMAKE_TOOLCHAIN_FILE=/w/tc.cmake \
  -DLLVM_NATIVE_TOOL_DIR=/w/native/bin -DCMAKE_BUILD_TYPE=MinSizeRel -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DLLVM_BUILD_SHARED_LIBS=OFF -DLLVM_ENABLE_PIC=OFF -DLLVM_BUILD_STATIC=ON -DLLVM_ENABLE_THREADS=OFF \
  -DLLVM_BUILD_RUNTIME=OFF -DLLVM_BUILD_TOOLS=OFF -DLLVM_INCLUDE_UTILS=OFF -DLLVM_BUILD_UTILS=OFF \
  -DLLVM_INCLUDE_RUNTIMES=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF -DLLVM_TARGETS_TO_BUILD=X86 \
  -DLLVM_DEFAULT_TARGET_TRIPLE=i386-unknown-linux-gnu $OFF -DLLVM_TOOL_LLVM_DRIVER_BUILD=ON \
  -DLLVM_TOOL_LLVM_AR_BUILD=ON -DLLVM_TOOL_LLVM_NM_BUILD=ON -DLLVM_TOOL_LLVM_OBJCOPY_BUILD=ON \
  -DLLVM_TOOL_LLVM_OBJDUMP_BUILD=ON -DLLVM_TOOL_LLVM_READOBJ_BUILD=ON \
  -DLLVM_ENABLE_PROJECTS="clang;lld" -DCLANG_ENABLE_ARCMT=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
  -DCLANG_INCLUDE_TESTS=OFF -DCLANG_BUILD_TOOLS=OFF -DCLANG_TOOL_CLANG_SCAN_DEPS_BUILD=OFF \
  -DCLANG_TOOL_CLANG_INSTALLAPI_BUILD=OFF -DCLANG_BUILD_EXAMPLES=OFF -DCLANG_INCLUDE_DOCS=OFF \
  -DCLANG_LINKS_TO_CREATE=clang -DLLD_BUILD_TOOLS=OFF -DDEFAULT_SYSROOT=/usr -DCLANG_RESOURCE_DIR=/usr \
  -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_PARALLEL_LINK_JOBS=1 >&2
start=$(date +%s)
ninja -C wasm llvm-driver >&2
echo "clang.wasm build: $(( $(date +%s) - start )) s" >&2
cat wasm/bin/llvm
BUILD
sed -i "s#/w/WASI_SDK#/w/$WASI_SDK#" "$tmp/w/build.sh"

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
	echo "gen-clang-wasm.sh: the image build failed (try $try of 3), again in 5 minutes" >&2
	sleep 300
done

tar -C "$tmp/w" -cf - . |
	docker run --rm -i "$image" sh -c 'mkdir /w && cd /w && tar -xf - && sh build.sh' \
		> "$tmp/out/clang.wasm"
rm -rf "$tmp/w"
mkdir -p "$dir"
if ! check "$tmp/out"; then
	# Kept for a look (or a new pin), never as the build.
	cp "$tmp/out/clang.wasm" "$dir/clang.wasm.rejected"
	exit 1
fi
rm -f "$dir"/clang.wasm "$dir"/LICENSE.*
cp "$tmp/out"/* "$dir/"
