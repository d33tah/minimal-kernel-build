#!/usr/bin/expect -f
log_user 0
set timeout 8
spawn qemu-system-x86_64 -cpu 486 -kernel minified/arch/x86/boot/bzImage -display curses -m 9M
expect {
    -re {Hello, World!} { exit 0 }
    timeout { exit 1 }
}
