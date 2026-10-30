#!/usr/bin/expect -f

# 60 s: under load the TCG boot (no KVM) can take well over 10 s.
set timeout 60
spawn qemu-system-x86_64 -kernel minified/arch/x86/boot/bzImage -display curses -m 18M

# The simple init writes "Hello, World!" and then "Still alive" via write(2).
# Both must appear, in that order. The init doesn't support interactivity.

expect {
    "Hello, World!" {}
    timeout { exit 1 }
    eof { exit 1 }
}

expect {
    "Still alive" { exit 0 }
    timeout { exit 1 }
    eof { exit 1 }
}
