#!/bin/bash
find minified -type d | while read dir; do
  c=$(find "$dir" -maxdepth 1 -name "*.c" 2>/dev/null | wc -l)
  o=$(find "$dir" -maxdepth 1 -name "*.o" 2>/dev/null | wc -l)
  if [ "$c" -gt 2 ] && [ "$o" -eq 0 ]; then
    loc=$(find "$dir" -maxdepth 1 -name "*.c" -o -name "*.h" 2>/dev/null | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}')
    echo "$dir: $c C files, $loc LOC, 0 objects"
  fi
done
