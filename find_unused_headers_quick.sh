#!/bin/bash
# Find headers that are never #included by any source file

cd minified || exit 1

echo "Finding all header files..."
find include -name "*.h" | sort > /tmp/all_headers.txt

echo "Finding headers that are #included..."
grep -rh "^#include" --include="*.c" --include="*.h" --include="*.S" . 2>/dev/null | \
    sed 's/#include[[:space:]]*["<]\([^">]*\)[">].*/\1/' | \
    sort -u > /tmp/included_headers.txt

echo "Finding potentially unused headers..."
while IFS= read -r header; do
    basename=$(basename "$header")
    if ! grep -q "$basename" /tmp/included_headers.txt; then
        echo "$header"
    fi
done < /tmp/all_headers.txt > /tmp/unused_headers.txt

echo "Found $(wc -l < /tmp/unused_headers.txt) potentially unused headers out of $(wc -l < /tmp/all_headers.txt) total"
head -50 /tmp/unused_headers.txt
