#!/usr/bin/env bash
set -Eeuo pipefail

# Capture one OV7251 source-6 direct CSI packet buffer and decode its packed
# RAW10 payload into a PNG. This script does not load modules or write BB8,
# sensor, PLL, port, or receiver registers.

DEVICE=${OV7251_DEVICE:-/dev/video5}
MEDIA=${OV7251_MEDIA:-/dev/media0}
WIDTH=${OV7251_WIDTH:-640}
HEIGHT=${OV7251_HEIGHT:-480}
STRIDE=${OV7251_STRIDE:-832}
HEADER_BYTES=${OV7251_HEADER_BYTES:-4}
DURATION=${OV7251_DURATION:-15}
PIXELFORMAT='Y10 '
OUT_DIR=${1:-${OV7251_OUTPUT_DIR:-/var/tmp/ov7251-ir-capture-$(date +%Y%m%d-%H%M%S)}}

usage() {
	cat <<'EOF'
Usage: scripts/capture-ov7251-ir-frame.sh [OUTPUT_DIR]

The verified source-6 direct-tap module with BB8 initialization must already
be loaded, and these links must already be enabled:

  ov7251 2-0060:0 -> Intel IPU4 CSI-2 1:0
  Intel IPU4 CSI-2 1:1 -> Intel IPU4 CSI-2 1 capture 0:0 (/dev/video5)

The script captures one 640x480 packed RAW10 buffer, removes the 4-byte
per-line header, and writes frame0.png plus raw data and diagnostics.

Environment overrides: OV7251_DEVICE, OV7251_MEDIA, OV7251_DURATION,
OV7251_WIDTH, OV7251_HEIGHT, OV7251_STRIDE, OV7251_HEADER_BYTES,
OV7251_OUTPUT_DIR.
EOF
}

if [[ ${1:-} == '-h' || ${1:-} == '--help' ]]; then
	usage
	exit 0
fi

die() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

[[ -c "$DEVICE" ]] || die "capture node does not exist: $DEVICE"
[[ -c "$MEDIA" ]] || die "media node does not exist: $MEDIA"
command -v v4l2-ctl >/dev/null || die "v4l2-ctl is required"
command -v media-ctl >/dev/null || die "media-ctl is required"
command -v timeout >/dev/null || die "timeout is required"
command -v python3 >/dev/null || die "python3 is required"
python3 -c 'import numpy; from PIL import Image' 2>/dev/null || \
	die "Python packages numpy and Pillow are required"

if v4l2-ctl -d "$DEVICE" --all >/dev/null 2>&1; then
	V4L2_USE_SUDO=0
else
	V4L2_USE_SUDO=1
fi
if media-ctl -d "$MEDIA" -p >/dev/null 2>&1; then
	MEDIA_USE_SUDO=0
else
	MEDIA_USE_SUDO=1
fi

run_v4l2() {
	if [[ $V4L2_USE_SUDO -eq 1 ]]; then
		sudo -n v4l2-ctl "$@"
	else
		v4l2-ctl "$@"
	fi
}
run_media() {
	if [[ $MEDIA_USE_SUDO -eq 1 ]]; then
		sudo -n media-ctl "$@"
	else
		media-ctl "$@"
	fi
}

graph=$(run_media -d "$MEDIA" -p 2>/dev/null) || \
	die "cannot enumerate $MEDIA; check video-device permissions"
grep -Fq -- '-> "Intel IPU4 CSI-2 1 capture 0":0' <<<"$graph" || \
	die "direct source-6 link is absent; load the debug-capture-links module"
grep -Fq -- '<- "ov7251 2-0060":0 [ENABLED]' <<<"$graph" || \
	die "OV7251-to-CSI link is not enabled"
grep -Fq -- '-> "Intel IPU4 CSI-2 1 capture 0":0 [ENABLED]' <<<"$graph" || \
	die "CSI-to-direct-capture link is not enabled"

mkdir -p "$OUT_DIR"
RAW=$OUT_DIR/capture.raw
PNG=$OUT_DIR/frame0.png
CAPTURE_LOG=$OUT_DIR/capture.log
KERNEL_LOG=$OUT_DIR/kernel.log
FORMAT_LOG=$OUT_DIR/format.txt
STATS=$OUT_DIR/stats.txt
RUN_INFO=$OUT_DIR/run.txt

run_media -d "$MEDIA" \
	-V '"Intel IPU4 CSI-2 1":0 [fmt:Y10_1X10/640x480 field:none]' >/dev/null
run_media -d "$MEDIA" \
	-V '"Intel IPU4 CSI-2 1":1 [fmt:Y10_1X10/640x480 field:none]' >/dev/null
run_v4l2 -d "$DEVICE" \
	--set-fmt-video="width=$WIDTH,height=$HEIGHT,pixelformat=$PIXELFORMAT"
run_v4l2 -d "$DEVICE" --get-fmt-video >"$FORMAT_LOG"

: >"$RAW"
start_iso=$(date --iso-8601=seconds)
printf 'device=%s\nmedia=%s\nstart=%s\n' "$DEVICE" "$MEDIA" "$start_iso" >"$RUN_INFO"
printf 'width=%s height=%s stride=%s header_bytes=%s\n' \
	"$WIDTH" "$HEIGHT" "$STRIDE" "$HEADER_BYTES" >>"$RUN_INFO"

set +e
if [[ $V4L2_USE_SUDO -eq 1 ]]; then
	timeout --signal=INT --kill-after=3s "$DURATION"s \
		sudo -n v4l2-ctl -d "$DEVICE" --stream-mmap=8 --stream-count=1 \
		--stream-to="$RAW" --verbose >"$CAPTURE_LOG" 2>&1
else
	timeout --signal=INT --kill-after=3s "$DURATION"s \
		v4l2-ctl -d "$DEVICE" --stream-mmap=8 --stream-count=1 \
		--stream-to="$RAW" --verbose >"$CAPTURE_LOG" 2>&1
fi
capture_rc=$?
set -e

if journalctl -k -b -o short-monotonic --since "$start_iso" >"$KERNEL_LOG" 2>/dev/null; then
	:
else
	sudo -n journalctl -k -b -o short-monotonic --since "$start_iso" >"$KERNEL_LOG" 2>/dev/null || :
fi
raw_bytes=$(stat -c %s "$RAW")
printf 'capture_rc=%s\nraw_bytes=%s\n' "$capture_rc" "$raw_bytes" >>"$RUN_INFO"

if [[ $capture_rc -ne 0 && $capture_rc -ne 124 ]]; then
	die "capture failed with status $capture_rc; see $CAPTURE_LOG"
fi

python3 - "$RAW" "$PNG" "$STATS" "$WIDTH" "$HEIGHT" "$STRIDE" "$HEADER_BYTES" <<'PY'
from pathlib import Path
import sys

import numpy as np
from PIL import Image

raw_path, png_path, stats_path = map(Path, sys.argv[1:4])
width, height, stride, header_bytes = map(int, sys.argv[4:8])
payload_bytes = width * 10 // 8
frame_bytes = stride * height
raw = raw_path.read_bytes()
if len(raw) < frame_bytes:
    raise SystemExit(f"short capture: {len(raw)} bytes, need {frame_bytes}")

rows = np.frombuffer(raw[:frame_bytes], dtype=np.uint8).reshape(height, stride)
packed = rows[:, header_bytes:header_bytes + payload_bytes]
groups = packed.reshape(height, width // 4, 5).astype(np.uint16)
pixels = np.empty((height, width), dtype=np.uint16)
for i in range(4):
    pixels[:, i::4] = (groups[:, :, i] << 2) | ((groups[:, :, 4] >> (i * 2)) & 3)

Image.fromarray((pixels >> 2).astype(np.uint8), mode="L").save(png_path)
headers = []
for row in range(min(8, height)):
    offset = row * stride
    header = int.from_bytes(raw[offset:offset + header_bytes], "little")
    headers.append(f"row{row}=0x{header:08x}")

stats_path.write_text(
    "\n".join([
        f"png={png_path}",
        f"raw_bytes={len(raw)}",
        f"frame_bytes_used={frame_bytes}",
        f"width={width} height={height} stride={stride} header_bytes={header_bytes}",
        f"pixel_min={int(pixels.min())}",
        f"pixel_max={int(pixels.max())}",
        f"pixel_mean={float(pixels.mean()):.4f}",
        f"pixel_std={float(pixels.std()):.4f}",
        "first_row_headers=" + ", ".join(headers),
    ]) + "\n"
)
print(stats_path.read_text(), end="")
PY

printf 'capture complete\nraw=%s\npng=%s\nlogs=%s %s\n' \
	"$RAW" "$PNG" "$CAPTURE_LOG" "$KERNEL_LOG"
cat "$STATS"
