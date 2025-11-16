#!/bin/bash
cd /home/user/minimal-kernel-build/minified

# Find all headers and check which are unused
for h in $(find include -name "*.h" -type f | sort); do
    name=$(basename "$h")

    # Count direct includes of this header (by exact path match)
    count=$(grep -r "#include.*$(echo $h | sed 's|include/||')" --include="*.c" --include="*.h" --include="*.S" \
            . 2>/dev/null | grep -v "^$h:" | wc -l)

    if [ "$count" -eq 0 ]; then
        loc=$(cat "$h" | wc -l)
        if [ "$loc" -gt 10 ]; then  # Only show files > 10 LOC
            echo "$loc $h"
        fi
    fi
done | sort -rn | head -50
