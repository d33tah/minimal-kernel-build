#!/bin/bash

git log --format=mboxrd -1 | egrep \^From\ \|Date | paste -d= -s

ps -p $( pgrep -x opencode ) -o lstart | paste -d= -s

git diff | diffstat | tail -n1

head -n15 FIXUP.md DIARY.md | cat -n
