#!/bin/bash
# Find unused static inline functions in a header file
cd /home/user/minimal-kernel-build

header="$1"
grep -oP '(?<=static inline [^(]+ )\w+(?=\()' "$header" 2>/dev/null | while read func; do
    # Count occurrences in C files, excluding the definition itself
    count=$(grep -rw "$func" minified --include="*.c" 2>/dev/null | wc -l)
    if [ "$count" -eq 0 ]; then
        echo "UNUSED: $func"
    fi
done
