#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# merge_config.sh - Merges config fragment values one by one.

set -e

clean_up() {
	rm -f $TMP_FILE
	rm -f $MERGE_FILE
}

RUNMAKE=true
CONFIG_PREFIX=${CONFIG_-CONFIG_}

while true; do
	case $1 in
	"-m")
		RUNMAKE=false
		shift
		continue
		;;
	*)
		break
		;;
	esac
done

if [ -z "$KCONFIG_CONFIG" ]; then
	KCONFIG_CONFIG=.config
fi

INITFILE=$1
shift;

if [ ! -r "$INITFILE" ]; then
	echo "The base file '$INITFILE' does not exist.  Exit." >&2
	exit 1
fi

MERGE_LIST=$*
SED_CONFIG_EXP1="s/^\(${CONFIG_PREFIX}[a-zA-Z0-9_]*\)=.*/\1/p"
SED_CONFIG_EXP2="s/^# \(${CONFIG_PREFIX}[a-zA-Z0-9_]*\) is not set$/\1/p"

TMP_FILE=$(mktemp ./.tmp.config.XXXXXXXXXX)
MERGE_FILE=$(mktemp ./.merge_tmp.config.XXXXXXXXXX)

echo "Using $INITFILE as base"

trap clean_up EXIT

cat $INITFILE > $TMP_FILE

for ORIG_MERGE_FILE in $MERGE_LIST ; do
	echo "Merging $ORIG_MERGE_FILE"
	if [ ! -r "$ORIG_MERGE_FILE" ]; then
		echo "The merge file '$ORIG_MERGE_FILE' does not exist.  Exit." >&2
		exit 1
	fi
	cat $ORIG_MERGE_FILE > $MERGE_FILE
	CFG_LIST=$(sed -n -e "$SED_CONFIG_EXP1" -e "$SED_CONFIG_EXP2" $MERGE_FILE)

	for CFG in $CFG_LIST ; do
		grep -q -w $CFG $TMP_FILE || continue
		PREV_VAL=$(grep -w $CFG $TMP_FILE)
		NEW_VAL=$(grep -w $CFG $MERGE_FILE)
		if [ "x$PREV_VAL" != "x$NEW_VAL" ] ; then
			echo Value of $CFG is redefined by fragment $ORIG_MERGE_FILE:
			echo Previous  value: $PREV_VAL
			echo New value:       $NEW_VAL
			echo
		fi
		sed -i "/$CFG[ =]/d" $TMP_FILE
	done
	cat $MERGE_FILE >> $TMP_FILE
done

if [ "$RUNMAKE" = "false" ]; then
	cp -T -- "$TMP_FILE" "$KCONFIG_CONFIG"
	echo "#"
	echo "# merged configuration written to $KCONFIG_CONFIG (needs make)"
	echo "#"
	exit
fi

make KCONFIG_ALLCONFIG=$TMP_FILE alldefconfig
