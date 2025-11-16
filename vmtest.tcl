#!/usr/bin/expect -f

set timeout 5
spawn qemu-system-x86_64 -kernel minified/arch/x86/boot/bzImage -display curses -m 19M

# When we see "Hello, World", test succeeded.
# The simple init doesn't support interactivity.

expect {
    "Hello, World!" { exit 0 }
    timeout { exit 1 }
}
