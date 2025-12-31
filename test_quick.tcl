#!/usr/bin/expect -f
# Usage: ./test_quick.tcl <memory_in_K>
# Returns 0 if kernel boots successfully, 1 if timeout

log_user 0
set timeout 5

if {$argc != 1} {
    puts "Usage: $argv0 <memory_in_K>"
    exit 2
}

set mem [lindex $argv 0]

spawn qemu-system-i386 -kernel minified/arch/x86/boot/bzImage -display curses -m ${mem}K

expect {
    -re {Hello, World!} { exit 0 }
    timeout { exit 1 }
}
