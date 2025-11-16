#!/bin/bash
cd /home/user/minimal-kernel-build/minified/include/linux
for h in $(ls *.h | head -100); do
    count=$(grep -r "#include.*$h" ../../.. 2>/dev/null | wc -l)
    if [ "$count" -eq "0" ]; then
        wc -l "$h" | awk "{print \$1, \"$h\"}"
    fi
done | sort -rn
