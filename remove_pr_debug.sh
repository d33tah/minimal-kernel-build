#!/bin/bash
# Remove all pr_debug statements from .c files
for file in $(find minified -name "*.c" -type f); do
    # Remove lines containing pr_debug
    sed -i '/pr_debug/d' "$file"
done
echo "Removed all pr_debug statements"
