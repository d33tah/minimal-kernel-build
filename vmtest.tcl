#!/usr/bin/expect -f

set timeout 5
spawn qemu-system-x86_64 -kernel minified/arch/x86/boot/bzImage -display curses -m 32M

# When we see "Hello, World", send a newline. If we get "Still alive",
# test succeeded. If we time out, exit with an error code.

expect {
    "Hello, World!" { send "\n" }
    timeout { exit 1 }
}

expect {
    "Still alive" { exit 0 }
    timeout { exit 1 }
}
