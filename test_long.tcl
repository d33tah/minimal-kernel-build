#!/usr/bin/expect -f
set timeout 120
spawn qemu-system-i386 -kernel minified/arch/x86/boot/bzImage -display curses -m 2M
expect {
    "Hello, World!" { puts "\nPASSED"; exit 0 }
    timeout { puts "\nTIMEOUT after 120s"; exit 1 }
}
