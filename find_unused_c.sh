#!/bin/bash
cd /home/user/minimal-kernel-build
for f in minified/kernel/*.c; do
    base=$(basename "$f" .c)
    if ! grep -q "$base" minified/kernel/Makefile; then
        echo "$f"
    fi
done
