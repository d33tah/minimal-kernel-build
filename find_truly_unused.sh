#!/bin/bash
cd /home/user/minimal-kernel-build/minified

# Find headers that have NO references in the codebase (except self)
for h in $(find include -name "*.h" -type f | sort); do
    base=$(basename "$h" .h)

    # Count all references to this base name
    count=$(grep -r "$base" . --include="*.c" --include="*.h" --include="*.S" 2>/dev/null | wc -l)

    # If only 1 reference (self-reference), it's truly unused
    if [ "$count" -eq 1 ]; then
        loc=$(cat "$h" | wc -l)
        if [ "$loc" -gt 15 ]; then
            echo "$loc $h"
        fi
    fi
done | sort -rn | head -30
