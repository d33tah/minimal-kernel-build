#!/bin/bash
cd minified/include
for dir in */; do
    count=$(find "$dir" -name "*.h" 2>/dev/null | wc -l)
    echo "$count $dir"
done | sort -rn
