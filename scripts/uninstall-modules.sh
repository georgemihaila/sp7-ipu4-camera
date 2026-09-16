#!/bin/sh
# Remove only unchanged module files recorded by install-modules.sh.
# Firmware is retained by default because it may be shared by another driver.
set -eu

KREL=${KREL:-$(uname -r)}
MODDIR=${MODDIR:-/lib/modules/$KREL/updates/extra}
MANIFEST="$MODDIR/.ipu4p-camera-modules"
ROOT=${MODULE_SOURCE_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
SOURCE_MANIFEST=${MODULE_SOURCE_MANIFEST:-$ROOT/modules/ipu4p-camera.modules}

[ -e "$MANIFEST" ] || {
	printf 'no IPU4P module manifest in %s; no modules removed\n' "$MODDIR"
	exit 0
}
[ -f "$MANIFEST" ] || { printf 'error: module manifest is not a regular file: %s\n' "$MANIFEST" >&2; exit 2; }

file_hash() { sha256sum "$1" | awk '{print $1}'; }
known_module() {
	[ "$1" = dw9719.ko ] && return 0
	[ -f "$SOURCE_MANIFEST" ] || return 1
	awk -F'|' -v module="$1" '$2 == module { found++ } END { exit found == 1 ? 0 : 1 }' "$SOURCE_MANIFEST"
}

# Reject malformed or unexpected manifest entries instead of allowing the
# manifest to turn this helper into a general-purpose rm command.
while IFS=' ' read -r name hash extra; do
	[ -n "$name" ] || continue
	known_module "$name" || { printf 'error: unexpected module in manifest: %s\n' "$name" >&2; exit 2; }
	[ -z "${extra:-}" ] && printf '%s\n' "$hash" | grep -Eq '^[0-9a-f]{64}$' || {
		printf 'error: malformed module manifest entry for %s\n' "$name" >&2; exit 2;
	}
	[ "$(awk -v n="$name" '$1 == n { c++ } END { print c + 0 }' "$MANIFEST")" -eq 1 ] || {
		printf 'error: duplicate module manifest entry: %s\n' "$name" >&2; exit 2;
	}
done < "$MANIFEST"

removed=0
retained=0
while IFS=' ' read -r name expected_hash extra; do
	[ -n "$name" ] || continue
	target="$MODDIR/$name"
	if [ ! -e "$target" ] && [ ! -L "$target" ]; then
		continue
	fi
	if [ ! -L "$target" ] && [ -f "$target" ] && [ "$(file_hash "$target")" = "$expected_hash" ]; then
		rm -f "$target"
		removed=$((removed + 1))
	else
		printf 'left modified or non-regular module in place: %s\n' "$target" >&2
		retained=$((retained + 1))
	fi
done < "$MANIFEST"

if [ "$retained" -eq 0 ]; then
	rm -f "$MANIFEST"
else
	# Keep only entries that still refer to files we deliberately retained.
	tmp="$MANIFEST.$$"
	: > "$tmp"
	while IFS=' ' read -r name expected_hash extra; do
		[ -n "$name" ] || continue
		target="$MODDIR/$name"
		if [ -e "$target" ] || [ -L "$target" ]; then
			printf '%s %s\n' "$name" "$expected_hash" >> "$tmp"
		fi
	done < "$MANIFEST"
	mv -f "$tmp" "$MANIFEST"
fi

if [ "$removed" -gt 0 ]; then depmod -a "$KREL"; fi
printf 'removed %s tracked IPU4P module(s) from %s; retained %s modified module(s); firmware was left in place\n' \
	"$removed" "$MODDIR" "$retained"
