#!/bin/sh
# Write site/kernels.json listing every site/kernels/<name>/bzImage, with
# kernels/<name>/symbols.json (from gen-symbols.py) when it exists.
# The page offers them in its kernel dropdown; the first one boots first.
set -eu
cd "$(dirname "$0")"
{
	printf '{"kernels": ['
	sep=
	seen=
	# hello first: the page boots the first entry.
	for f in kernels/hello/bzImage kernels/*/bzImage; do
		case " $seen " in *" $f "*) continue ;; esac
		seen="$seen $f"
		[ -f "$f" ] || continue
		name=${f#kernels/}
		name=${name%/bzImage}
		sym=kernels/$name/symbols.json
		if [ -f "$sym" ]; then
			printf '%s\n  {"name": "%s", "path": "%s", "symbols": "%s"}' "$sep" "$name" "$f" "$sym"
		else
			printf '%s\n  {"name": "%s", "path": "%s"}' "$sep" "$name" "$f"
		fi
		sep=,
	done
	printf '\n]}\n'
} > kernels.json
cat kernels.json
