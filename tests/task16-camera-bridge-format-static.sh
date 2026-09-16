#!/usr/bin/env bash
# Static regression checks for loopback format negotiation.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BRIDGE=$ROOT/scripts/surface-camera-bridge.py

python3 -m py_compile "$BRIDGE"
grep -Fq '"/usr/bin/v4l2-ctl", "-d", device, "--get-fmt-video"' "$BRIDGE"
grep -Fq 'for format_name in ("MJPG", "JPEG", "YUYV")' "$BRIDGE"
grep -Fq '"!", "jpegenc", "quality=85", "!", "jpegparse", "!"' "$BRIDGE"
grep -Fq 'image/jpeg,parsed=true' "$BRIDGE"
grep -Fq '"!", "videoscale"' "$BRIDGE"
grep -Fq 'format=YUY2' "$BRIDGE"
# The old fixed caps must not be present in either producer as a single path.
! grep -Fq 'videoconvert", "!", "video/x-raw,format=YUY2' "$BRIDGE"
printf '%s\n' 'PASS: bridge follows YUYV or MJPG loopback format and encodes MJPG'
