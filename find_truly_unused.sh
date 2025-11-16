#!/bin/bash
cd minified
find . -name "*.c" -type f | while read f; do
  base=$(basename "$f")
  # Check if in any Makefile
  if grep -q "$base" $(find . -name "Makefile" -o -name "Makefile.*") 2>/dev/null; then
    continue
  fi
  # Check if included by any .c file
  if grep -rq "#include \"$base\"" --include="*.c" . 2>/dev/null; then
    continue
  fi
  # Count lines
  lines=$(wc -l < "$f")
  echo "$lines $f"
done | sort -rn
