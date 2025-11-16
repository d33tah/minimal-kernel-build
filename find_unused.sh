#!/bin/bash
cd minified
for h in include/linux/*.h; do
    name=$(basename "$h")
    count=$(grep -r "include.*$name" --include="*.c" --include="*.h" . 2>/dev/null | grep -v "^$h:" | wc -l)
    if [ "$count" -eq 0 ]; then
        wc -l "$h"
    fi
done | sort -rn | head -30
