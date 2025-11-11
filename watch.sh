#!/bin/bash

git log --format=mboxrd -1 | egrep \^From\ \|Date | paste -d= -s

for pid in `pgrep -x claude`; do
	ps -p $pid -o lstart | paste -d= -s
done

git diff | diffstat | tail -n1

head -n15 FIXUP.md DIARY.md | cat -n
