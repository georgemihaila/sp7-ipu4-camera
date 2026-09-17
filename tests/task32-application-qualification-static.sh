#!/bin/sh
# Static contract checks for Phase 4 application qualification.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOC="$ROOT/docs/application-qualification.md"
README="$ROOT/README.md"
UPSTREAM="$ROOT/docs/upstream-readiness.md"
SPEC="$ROOT/docs/native-webcam-driver-spec.md"

test -f "$DOC"
test -f "$README"
test -f "$UPSTREAM"
test -f "$SPEC"
sh -n "$0"

for app in "GNOME Snapshot" "Chromium/WebRTC" "Zoom Linux client" \
	"Discord Linux client" "Direct V4L2" 'Native `cam`'; do
	grep -Fq "$app" "$DOC"
done

for field in "Package/runtime and sandbox" "Selected physical camera identity" \
	"Preview movement and capture" "Measured FPS" "Lens-cover/content check" \
	"Front/rear switch" "Close/reopen" "Relaunch" "Portal permission" \
	"Kernel/PipeWire log IDs"; do
	grep -Fq "$field" "$DOC"
done

for result in PASS FAIL NOT\ TESTED ENUMERATED-ONLY; do
	grep -Fq "$result" "$DOC"
done

# The matrix must be reproducible and must resolve nodes instead of baking in
# a current /dev/videoN or /dev/mediaN minor number.
grep -Fq '/dev/media*' "$DOC"
grep -Fq '/sys/class/video4linux/video*' "$DOC"
grep -Fq 'media-ctl' "$DOC"
grep -Fq 'v4l2-ctl' "$DOC"
grep -Fq 'cam -l' "$DOC"
grep -Fq 'pw-dump' "$DOC"
grep -Fq 'journalctl -k' "$DOC"
grep -Fq 'journalctl --user' "$DOC"
if grep -Eq '/dev/video[0-9]|/dev/media[0-9]|/dev/v4l-subdev[0-9]' "$DOC"; then
	echo "hard-coded media/video/sub-device minor found in application contract" >&2
	exit 1
fi

# A positive support/works claim must never be paired with enumeration or a
# bridge/loopback path. Negative boundary statements are allowed and are
# checked below by their explicit unclaimed wording.
for document in "$DOC" "$README" "$UPSTREAM" "$SPEC"; do
	if grep -Eiq '(application|client|webcam).*(support(ed)?|works).*(enumerat|loopback|c[ -]?bridge)|(enumerat|loopback|c[ -]?bridge).*(application|client|webcam).*(support(ed)?|works)' "$document"; then
		echo "support claim is coupled to enumeration or bridge evidence: $document" >&2
		exit 1
	fi
done

grep -Fq 'no application support claim' "$DOC"
grep -Fq 'support remains unclaimed' "$README"
grep -Fq 'support remains unclaimed' "$UPSTREAM"
grep -Fq 'docs/application-qualification.md' "$README"
grep -Fq 'docs/application-qualification.md' "$UPSTREAM"

echo 'PASS [application-qualification-static] per-application evidence matrix and no-enumeration/no-bridge support boundary are present'
