#!/bin/sh
# Write site/kernels.json listing every site/kernels/<name>/bzImage, with
# kernels/<name>/symbols.json and disasm.json (gen-symbols.py, gen-disasm.py) when present.
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
		extra=
		sym=kernels/$name/symbols.json
		dis=kernels/$name/disasm.json
		[ -f "$sym" ] && extra="$extra, \"symbols\": \"$sym\""
		[ -f "$dis" ] && extra="$extra, \"disasm\": \"$dis\""
		printf '%s\n  {"name": "%s", "path": "%s"%s}' "$sep" "$name" "$f" "$extra"
		sep=,
	done
	printf '\n]}\n'
} > kernels.json
cat kernels.json
