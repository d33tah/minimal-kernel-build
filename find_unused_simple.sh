#!/bin/bash
cd /home/user/minimal-kernel-build/minified

for h in include/linux/*.h; do
    name=$(basename "$h")
    # Count includes of this header in source files (excluding the header itself)
    count=$(grep -r "#include.*$name" --include="*.c" --include="*.h" --include="*.S" \
            include/ lib/ kernel/ fs/ mm/ drivers/ arch/ 2>/dev/null | \
            grep -v "^$h:" | wc -l)

    if [ "$count" -eq 0 ]; then
        loc=$(cat "$h" | wc -l)
        echo "$loc $h"
    fi
done | sort -rn | head -30
