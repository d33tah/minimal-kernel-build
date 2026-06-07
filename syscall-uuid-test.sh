#!/bin/bash
# Anti-cheat guard: prove "Hello, World!" is printed by the init ELF via the
# write(2) syscall (int 0x80), NOT hardcoded in the bootloader/decompressor.
#
# It swaps the printed string in elo/init.nasm for a fresh per-run UUID, rebuilds
# the init ELF and the kernel, boots under qemu, and asserts the UUID appears in
# the console output. If the string were printed by a decompressor stub instead
# of the running init, the swapped UUID would never show up and this fails.
# Note: no `pipefail` — `yes "" | make olddefconfig` makes `yes` die with SIGPIPE
# (exit 141) once make stops reading, which would abort the script spuriously.
set -eu

MARKER="syscall-$(cat /proc/sys/kernel/random/uuid)"
echo ">>> anti-cheat marker: $MARKER"

# 1) swap the string the init ELF prints
sed -i "s/Hello, World!/$MARKER/" minified/elo/init.nasm
grep -q "$MARKER" minified/elo/init.nasm

# 2) rebuild the init ELF from its source
( cd minified/elo && nasm -f bin -o init init.nasm && chmod +x init )

# 3) rebuild the kernel so the initramfs repacks the new /init
( cd minified && make LLVM=1 tinyconfig && yes "" | make LLVM=1 olddefconfig && make LLVM=1 -j"$(nproc)" )

# 4) boot and assert the marker is printed (by init, via syscall)
cat > /tmp/uuidtest.tcl <<EOF
#!/usr/bin/expect -f
set timeout 30
spawn qemu-system-x86_64 -kernel minified/arch/x86/boot/bzImage -display curses -m 18M
expect {
    "$MARKER" { exit 0 }
    timeout   { exit 1 }
}
EOF

if TERM=xterm expect /tmp/uuidtest.tcl; then
    echo ">>> PASS: marker printed by init via syscall — not a decompressor stub"
else
    echo ">>> FAIL: marker not seen. The gate string is NOT coming from the init ELF."
    echo ">>>       Likely a hardcoded bootloader/decompressor print (cheat)."
    exit 1
fi
