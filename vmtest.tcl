#!/usr/bin/env tclsh
package require Expect
spawn qemu-system-x86_64 -kernel minified/arch/x86/boot/bzImage -display curses -m 18M
expect {
    "Hello, World!" { exit 0 }
    timeout { exit 1 }
}
