#!/bin/sh
# Read-only Phase 3 validation of a native libcamera PipeWire source.
#
# The validator deliberately uses the current node name from pw-dump rather
# than a PipeWire object id or a V4L2 device minor. It only creates a bounded
# GStreamer consumer and temporary frame files; it does not alter services,
# links, modules, or the camera bridge.
set -u

MODE=live
TIMEOUT=${CAMERA_TEST_TIMEOUT:-20}
FRAME_COUNT=${NATIVE_PIPEWIRE_FRAMES:-3}
CAPS=${NATIVE_PIPEWIRE_CAPS:-video/x-raw,format=RGB,width=640,height=480,framerate=30/1}
OUT=${NATIVE_PIPEWIRE_VALIDATION_DIR:-${OUTPUT_DIR:-}}

say() { printf '%s\n' "$*"; }
skip() { say "SKIP [$1] $2"; }
pass() { say "PASS [$1] $2"; }
fail() { say "FAIL [$1] $2"; printf '%s\n' failed >"$FAILED_MARK"; }

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
	say "PASS [native-pipewire-static] script syntax and contract are checked by the static suite"
	exit 0
fi

case $FRAME_COUNT in
	''|*[!0-9]*) say "invalid NATIVE_PIPEWIRE_FRAMES: $FRAME_COUNT" >&2; exit 2 ;;
esac
[ "$FRAME_COUNT" -gt 1 ] 2>/dev/null || {
	say "invalid NATIVE_PIPEWIRE_FRAMES: $FRAME_COUNT (expected at least 2)" >&2
	exit 2
}

FAILED_MARK=
KEEP=1
if [ -z "$OUT" ]; then
	OUT=$(mktemp -d "${TMPDIR:-/tmp}/sp7-native-pipewire.XXXXXX") || exit 1
	KEEP=0
else
	if ! mkdir -p "$OUT"; then
		say "FAIL [native-pipewire] cannot create output directory: $OUT" >&2
		exit 1
	fi
fi
FAILED_MARK=$OUT/failed
: >"$FAILED_MARK"

cleanup() {
	status=$?
	trap - EXIT HUP INT TERM
	if [ "$KEEP" -eq 0 ] && [ ! -s "$FAILED_MARK" ]; then
		rm -rf "$OUT"
	fi
	exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

missing=
for tool in pw-dump jq gst-launch-1.0 gst-inspect-1.0 timeout cksum dd head tail tr wc awk sed mkdir mktemp; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		missing=${missing:+$missing, }$tool
	fi
done
if [ -n "$missing" ]; then
	skip tools "required PipeWire/GStreamer tools are unavailable: $missing"
	exit 0
fi

if ! gst-inspect-1.0 pipewiresrc >"$OUT/gst-pipewiresrc.txt" 2>&1 ||
	! gst-inspect-1.0 videoconvert >"$OUT/gst-videoconvert.txt" 2>&1 ||
	! gst-inspect-1.0 multifilesink >"$OUT/gst-multifilesink.txt" 2>&1; then
	skip tools "required GStreamer pipewiresrc, videoconvert, or multifilesink is unavailable"
	exit 0
fi

if ! pw-dump >"$OUT/pw-dump.json" 2>&1; then
	skip monitor "pw-dump could not read the current PipeWire graph"
	exit 0
fi

# WirePlumber's native monitor marks camera nodes with libcamera properties
# and creates Video/Source nodes. The node.name is transient but is the
# supported target-object selector for pipewiresrc; no object serial is used.
if ! jq -e 'type == "array"' "$OUT/pw-dump.json" >/dev/null 2>&1; then
	skip monitor "PipeWire graph output is not valid JSON"
	exit 0
fi
jq -r '
	.[]
	| select((.type // "") | test("^PipeWire:Interface:Node"))
	| (.info.props // {}) as $p
	| select(($p["media.class"] // "") == "Video/Source")
	| select(
		(($p["api.libcamera.location"] // "") != "") or
		(($p["factory.name"] // "") | test("libcamera"; "i")) or
		(($p["node.name"] // "") | test("^libcamera_(input|source)"; "i")) or
		(($p["device.name"] // "") | test("^libcamera"; "i"))
	)
	| select(($p["node.name"] // "") != "")
	| [$p["node.name"], ($p["node.description"] // ""), ($p["api.libcamera.location"] // "")]
	| @tsv
' "$OUT/pw-dump.json" >"$OUT/native-nodes.tsv"

if [ ! -s "$OUT/native-nodes.tsv" ]; then
	skip monitor "native libcamera monitor is unavailable or has no Video/Source nodes"
	exit 0
fi

say "Native PipeWire validation (read-only; requested caps: $CAPS)"
TAB=$(printf '\t')
node_count=0
while IFS="$TAB" read -r node description location; do
	[ -n "$node" ] || continue
	node_count=$((node_count + 1))
	slug=$(printf '%s' "$node" | cksum | awk '{ print $1 }')
	node_dir=$OUT/node-$slug
	mkdir -p "$node_dir" || { fail "node-$slug" "cannot create diagnostics directory"; continue; }
	say "INFO [node-$slug] name=$node description=$description location=${location:-unknown}"

	# num-buffers and timeout bound both the source request and the test run.
	# videoconvert requests a common RGB viewfinder format before recording one
	# temporary raw file per negotiated video buffer.
	if timeout --signal=TERM --kill-after=2 "$TIMEOUT" \
		gst-launch-1.0 -q -e \
			pipewiresrc target-object="$node" num-buffers="$FRAME_COUNT" \
				do-timestamp=true ! videoconvert ! "$CAPS" ! \
				multifilesink location="$node_dir/frame-%02d.rgb" \
			>"$node_dir/gst.log" 2>&1; then
		set -- "$node_dir"/frame-*.rgb
		if [ ! -f "$1" ] || [ "$#" -ne "$FRAME_COUNT" ]; then
			fail "node-$slug" "PipeWire source negotiated without the expected frame set (see $node_dir/gst.log)"
			continue
		fi

		previous=
		valid=1
		for frame do
			bytes=$(wc -c <"$frame" | tr -d '[:space:]')
			nonzero=$(LC_ALL=C tr -d '\000' <"$frame" | wc -c | tr -d '[:space:]')
			checksum=$(cksum "$frame" | awk '{ print $1 ":" $2 }')
			if [ -z "$bytes" ] || [ "$bytes" -le 0 ] 2>/dev/null ||
				[ -z "$nonzero" ] || [ "$nonzero" -le 0 ] 2>/dev/null; then
				valid=0
			fi
			if [ -n "$previous" ] && [ "$checksum" = "$previous" ]; then
				valid=0
			fi
			previous=$checksum
		done
		if [ "$valid" -eq 1 ]; then
			pass "node-$slug" "$FRAME_COUNT advancing frames negotiated with non-black RGB image data"
		else
			fail "node-$slug" "discovered native source produced black or static frames"
		fi
	else
		fail "node-$slug" "discovered native source could not complete bounded PipeWire/GStreamer preview (see $node_dir/gst.log)"
	fi
done <"$OUT/native-nodes.tsv"

if [ "$node_count" -eq 0 ]; then
	skip monitor "native node inventory contained no selectable node names"
	exit 0
fi

if [ -s "$FAILED_MARK" ]; then
	exit 1
fi
if [ "$KEEP" -eq 1 ]; then
	say "RESULT: PASS (native PipeWire nodes validated; reports retained at $OUT)"
else
	say "RESULT: PASS (native PipeWire nodes validated; temporary reports removed)"
fi
