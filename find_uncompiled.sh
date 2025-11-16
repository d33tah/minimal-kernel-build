#!/bin/bash
cd /home/user/minimal-kernel-build/minified
find lib -name "*.c" -type f | while read f; do
    o="${f%.c}.o"
    if [ ! -f "$o" ]; then
        wc -l "$f"
    fi
done | sort -rn
