#!/usr/bin/expect -f

set timeout 10
spawn qemu-system-x86_64 -kernel minified/arch/x86/boot/bzImage -display curses -m 32M

expect {
    "Hello, World2" {
        exit 0
    }
    timeout {
        send_user "Timed out waiting for 'Hello, World'\n"
        exit 1
    }
}
