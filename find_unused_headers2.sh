#!/bin/bash
cd /home/user/minimal-kernel-build/minified

# Check all header files
find include -name "*.h" -type f | while read h; do
    hbase=$(basename "$h")
    count=$(grep -r "#include.*$hbase" . --include="*.c" --include="*.h" --include="*.S" 2>/dev/null | grep -v "^$h:" | wc -l)
    if [ "$count" -eq "0" ]; then
        loc=$(wc -l < "$h")
        echo "$loc $h"
    fi
done | sort -rn | head -50
