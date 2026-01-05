#!/usr/bin/expect -f
set timeout 30
spawn qemu-system-i386 -kernel minified/arch/x86/boot/bzImage -display curses -m 2M
expect {{Hello, World!} {exit 0} timeout {exit 1}}
