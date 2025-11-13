#!/bin/bash
# Find headers that are never included in the codebase

cd minified

for header in $(find include -name "*.h" | sort); do
    # Extract just the filename part after include/
    header_path="${header#include/}"

    # Search for includes of this header in all C files and headers
    # Look for both #include <header> and #include "header"
    found=$(find . -name "*.c" -o -name "*.h" | xargs grep -l "#include [<\"]${header_path}[>\"]" 2>/dev/null | wc -l)

    if [ "$found" -eq 0 ]; then
        # Get LOC count
        loc=$(cat "$header" | wc -l)
        echo "$loc $header"
    fi
done | sort -rn | head -30
