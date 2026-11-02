#!/bin/sh
# Write site/kernels.json listing every site/kernels/<name>/bzImage, with
# kernels/<name>/symbols.json, disasm.json and sources.json (gen-symbols.py,
# gen-disasm.py, gen-source.py), kernels/<name>/initrd.img (the initrd the
# page loads with that kernel) and the members of kernels/<name>/meta.json
# (label, cmdline, ram) when present. A directory with a file "base"
# (holding another kernel's name) and no bzImage is a variant of that
# kernel: same bzImage and symbol files, its own initrd and meta.json.
# The page offers them in its kernel dropdown; the first one boots first.
set -eu
cd "$(dirname "$0")"
{
	printf '{"kernels": ['
	sep=
	seen=
	# hello first: the page boots the first entry.
	for d in kernels/hello kernels/*; do
		case " $seen " in *" $d "*) continue ;; esac
		seen="$seen $d"
		name=${d#kernels/}
		src=$d
		[ -f "$d/base" ] && src=kernels/$(cat "$d/base")
		f=$src/bzImage
		[ -f "$f" ] || continue
		extra=
		[ -f "$src/symbols.json" ] && extra="$extra, \"symbols\": \"$src/symbols.json\""
		[ -f "$src/disasm.json" ] && extra="$extra, \"disasm\": \"$src/disasm.json\""
		[ -f "$src/sources.json" ] && extra="$extra, \"sources\": \"$src/sources.json\""
		[ -f "$d/initrd.img" ] && extra="$extra, \"initrd\": \"$d/initrd.img\""
		# Per-kernel defaults (label, cmdline, ram): kernels/<name>/meta.json,
		# a flat JSON object whose members are copied into the entry.
		meta=$d/meta.json
		if [ -f "$meta" ]; then
			members=$(tr -d '\n' < "$meta" | sed 's/^[[:space:]]*{//; s/}[[:space:]]*$//')
			[ -n "$members" ] && extra="$extra, $members"
		fi
		printf '%s\n  {"name": "%s", "path": "%s"%s}' "$sep" "$name" "$f" "$extra"
		sep=,
	done
	printf '\n]}\n'
} > kernels.json
cat kernels.json
