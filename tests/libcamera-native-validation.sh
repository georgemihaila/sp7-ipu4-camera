#!/bin/sh
# Read-only native libcamera validation through the installed cam CLI.
#
# This qualifies the distribution's native camera path (currently Simple +
# SoftISP). It does not load modules, change media links, stop services, or
# use the project's application-compatibility bridge.
set -u

MODE=live
TIMEOUT=${CAMERA_TEST_TIMEOUT:-30}
OUT=${LIBCAMERA_VALIDATION_DIR:-${OUTPUT_DIR:-}}

say() { printf '%s\n' "$*"; }
skip() { say "SKIP [$1] $2"; }
fail() { say "FAIL [$1] $2"; FAILED=$((FAILED + 1)); }
pass() { say "PASS [$1] $2"; }

while [ "$#" -gt 0 ]; do
	case $1 in
		--live) MODE=live ;;
		--static) MODE=static ;;
		--help|-h)
			say "usage: $0 [--live|--static]"
			exit 0
			;;
		*) say "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

if [ "$MODE" = static ]; then
	say "PASS [libcamera-native-static] script syntax and contract are checked by the static suite"
	exit 0
fi

FAILED=0
KEEP=1
if [ -z "$OUT" ]; then
	OUT=$(mktemp -d "${TMPDIR:-/tmp}/sp7-libcamera-validation.XXXXXX") || exit 1
	KEEP=0
else
	if ! mkdir -p "$OUT"; then
		say "FAIL [libcamera-native] cannot create output directory: $OUT" >&2
		exit 1
	fi
fi

LIST=$OUT/cam-list.txt
CAMERAS=$OUT/cameras.tsv
trap 'status=$?; if [ "$KEEP" -eq 0 ] && [ "$FAILED" -eq 0 ]; then rm -rf "$OUT"; fi; exit "$status"' EXIT HUP INT TERM

missing=
for tool in cam timeout awk cksum dd head mkdir sed tail tr wc; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		missing=${missing:+$missing, }$tool
	fi
done
if [ -n "$missing" ]; then
	skip tools "required native validation tools are unavailable: $missing"
	exit 0
fi

if ! cam -l >"$LIST" 2>&1; then
	skip cameras "cam -l could not enumerate native cameras"
	exit 0
fi

# cam's list format is "N: name (camera-id)".  The numeric display index is
# deliberately discarded; the entity/id text is the selector used for capture.
awk '
function trim(s) { sub(/^[[:space:]]+/, "", s); sub(/[[:space:]]+$/, "", s); return s }
/^[[:space:]]*[0-9]+:[[:space:]]+/ {
	line=$0
	sub(/^[[:space:]]*[0-9]+:[[:space:]]+/, "", line)
	line=trim(line)
	name=line
	id=""
	if (line ~ /\([^()]+\)[[:space:]]*$/) {
		id=line
		sub(/^.*\(/, "", id)
		sub(/\)[[:space:]]*$/, "", id)
		sub(/[[:space:]]+\([^()]+\)[[:space:]]*$/, "", name)
	}
	name=trim(name)
	id=trim(id)
	if (name != "" && id != "") print name "\t" id
}
' "$LIST" >"$CAMERAS"

if [ ! -s "$CAMERAS" ]; then
	skip cameras "cam -l reported no native camera with a selectable entity/id"
	exit 0
fi

classify() {
	name=$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')
	id=$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')
	case "$name $id" in
		*front*|*camf*) printf '%s\n' front ;;
		*rear*|*back*|*camr*) printf '%s\n' rear ;;
		*) printf '%s\n' unknown ;;
	esac
}

validate_ppm() {
	file=$1
	[ -s "$file" ] || return 1

	# cam --file=...ppm emits a binary P6 image.  Parse the three-line header
	# instead of assuming a frame size; the payload must match the header.
	header=$(head -c 256 "$file" | sed -n '1,3p')
	printf '%s\n' "$header" | awk '
	NR == 1 && $0 == "P6" { ok1=1 }
	NR == 2 && $1 ~ /^[0-9]+$/ && $2 ~ /^[0-9]+$/ { w=$1; h=$2; ok2=1 }
	NR == 3 && $0 == "255" { ok3=1 }
	END { exit !(ok1 && ok2 && ok3 && w > 0 && h > 0) }
	' || return 1

	width=$(printf '%s\n' "$header" | awk 'NR == 2 { print $1 }')
	height=$(printf '%s\n' "$header" | awk 'NR == 2 { print $2 }')
	header_bytes=$(printf '%s\n' "$header" | wc -c | tr -d '[:space:]')
	payload_bytes=$(wc -c <"$file" | tr -d '[:space:]')
	expected=$((width * height * 3))
	[ "$payload_bytes" -eq $((header_bytes + expected)) ] || return 1

	# Skip the parsed header.  Every processed frame must contain actual image
	# bytes, and not merely a valid PPM header followed by zero-filled buffers.
	start=$((header_bytes + 1))
	nonzero=$(tail -c +"$start" "$file" | LC_ALL=C tr -d '\000' | wc -c | tr -d '[:space:]')
	case $nonzero in ''|*[!0-9]*) return 1 ;; esac
	[ "$nonzero" -gt 0 ]
}

capture_camera() {
	selector=$1
	directory=$2
	count=$3
	mkdir -p "$directory" || return 1
	# These options were verified against the host's installed cam v0.7.1:
	# --file= is required because --file has an optional argument; the stream
	# request keeps the validation bounded while remaining processed output.
	timeout --signal=TERM --kill-after=2 "$TIMEOUT" \
		cam --camera="$selector" --stream=width=640,height=480 \
			--capture="$count" --file="$directory/frame-#.ppm" \
			>"$directory/capture.log" 2>&1
}

validate_capture_set() {
	directory=$1
	count=$2
	set -- "$directory"/*.ppm
	[ -f "$1" ] || return 1
	[ "$#" -eq "$count" ] || return 1

	previous=
	for file do
		validate_ppm "$file" || return 1
		checksum=$(cksum "$file" | awk '{ print $1 ":" $2 }')
		if [ -n "$previous" ] && [ "$checksum" = "$previous" ]; then
			return 1
		fi
		previous=$checksum
	done
}

validate_reopen_capture() {
	directory=$1
	set -- "$directory"/*.ppm
	[ -f "$1" ] || return 1
	[ "$#" -eq 1 ] || return 1
	validate_ppm "$1"
}

say "Native libcamera validation (read-only; processed PPM via cam)"
say "cam list: $LIST"
cat "$LIST"

while IFS="$(printf '\t')" read -r name selector; do
	[ -n "$name" ] || continue
	location=$(classify "$name" "$selector")
	slug=$location
	[ "$slug" = unknown ] && slug=camera-$(printf '%s' "$selector" | cksum | awk '{ print $1 }')
	primary=$OUT/$slug-primary
	reopen=$OUT/$slug-reopen
	say "INFO [$slug] name=$name entity=$selector location=$location"

	if capture_camera "$selector" "$primary" 3 && validate_capture_set "$primary" 3; then
		pass "capture-$slug" "three processed frames have valid nonzero payloads and changing image content"
	else
		fail "capture-$slug" "discovered $location camera produced invalid, black, or static processed frames (see $primary/capture.log)"
		continue
	fi

	if capture_camera "$selector" "$reopen" 1 && validate_reopen_capture "$reopen"; then
		pass "reopen-$slug" "release/reopen processed capture completed with nonzero image payload"
	else
		fail "reopen-$slug" "release/reopen capture produced invalid or black processed output (see $reopen/capture.log)"
	fi
done <"$CAMERAS"

[ "$FAILED" -eq 0 ] || exit 1
if [ "$KEEP" -eq 1 ]; then
	say "RESULT: PASS (reports retained at $OUT)"
else
	say "RESULT: PASS (native cameras validated; temporary reports removed)"
fi
