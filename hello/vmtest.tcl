#!/usr/bin/expect -f

# Same check as the repo's ../vmtest.tcl: boot the image in qemu with the
# curses display and wait for both strings on the VGA text screen, in order.
# Optional arguments: kernel image (default bzImage next to this script)
# and memory size (default 18M, as the gate uses).

set timeout 60
set dir [file dirname [info script]]
set kernel [lindex [concat $argv [list "$dir/bzImage"]] 0]
set mem [lindex [concat [lrange $argv 1 1] [list 18M]] 0]
spawn qemu-system-i386 -kernel $kernel -display curses -m $mem

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
