#!/bin/bash
# Init-provenance (anti-cheat) guard: prove the gate string is printed by the
# /init ELF via the write(2) syscall (int 0x80), not hardcoded in the
# bootloader, the decompressor or any kernel stub.
#
# It swaps the string that minified/elo/init.nasm prints for a fresh per-run
# UUID marker, rebuilds the init ELF and the kernel (the initramfs repacks the
# new /init), boots under qemu and asserts that the marker appears on the
# curses console. A stub that prints a fixed "Hello, World!" cannot know the
# marker, so this fails unless the text really comes from the running /init.
#
# WARNING: this script edits minified/elo/init.nasm and rebuilds
# minified/elo/init IN PLACE. It is meant for CI (the docker image) or for a
# scratch copy (e.g. `git archive HEAD | tar -x -C /some/scratch/dir`). Do not
# run it in a working tree you care about.
#
# Run from anywhere; it works relative to the directory holding this script.
set -euo pipefail

cd "$(dirname "$0")"

# 12 hex digits (48 bits) of a fresh UUID: unguessable, yet short enough that
# it stays well inside one 80-column console line.
uuid="$(cat /proc/sys/kernel/random/uuid)"
MARKER="syscall-${uuid:0:8}${uuid:9:4}"
echo ">>> provenance marker: $MARKER"

# 1) swap the string the init ELF prints
sed -i "s/Hello, World!/$MARKER/" minified/elo/init.nasm
grep -q "$MARKER" minified/elo/init.nasm
if grep -q 'Hello, World!' minified/elo/init.nasm; then
	echo ">>> FAIL: init.nasm still contains the original string" >&2
	exit 1
fi

# 2) rebuild the init ELF from its source
(cd minified/elo && nasm -f bin -o init init.nasm && chmod +x init)
grep -q "$MARKER" minified/elo/init

# 3) rebuild the kernel so the initramfs repacks the new /init
(cd minified && make LLVM=1 -j"$(nproc)")

# 4) boot and assert the marker is printed (by /init, via write(2)).
# Up to 5 boots: on hosts without KVM an occasional boot hangs before /init
# runs and prints nothing at all. A retry cannot help a cheat, because a stub
# that does not run /init never prints the marker on any attempt.
tcl="$(mktemp)"
trap 'rm -f "$tcl"' EXIT
cat >"$tcl" <<TCL
#!/usr/bin/expect -f
set timeout 60
spawn qemu-system-x86_64 -kernel minified/arch/x86/boot/bzImage -display curses -m 18M
expect {
	"$MARKER" {}
	timeout { exit 1 }
	eof { exit 1 }
}
expect {
	"Still alive" { exit 0 }
	timeout { exit 1 }
	eof { exit 1 }
}
TCL

for attempt in 1 2 3 4 5; do
	echo ">>> boot attempt $attempt"
	if TERM="${TERM:-xterm}" expect "$tcl"; then
		echo ">>> PASS: marker printed by /init via write(2)"
		exit 0
	fi
	echo ">>> attempt $attempt: marker not seen within 60 s"
done
echo ">>> FAIL: marker not seen on the console in 5 boots." >&2
echo ">>>       The gate string does not come from the /init ELF." >&2
exit 1
