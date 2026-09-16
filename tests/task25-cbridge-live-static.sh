#!/bin/sh
# Static contract checks for the shell-only live C-bridge qualification.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LIVE="$ROOT/tests/cbridge-live-qualification.sh"

test -f "$LIVE"
sh -n "$LIVE"

# The live harness must not revive the removed Python bridge or place captures
# in the checkout. Lower-case python is checked because the explanatory prose
# deliberately uses a capitalized product name.
if grep -Eq '(^|[[:space:];|])(python|python3)([[:space:]]|$)|[[:alnum:]_.-]+\.py' "$LIVE"; then
	echo 'task25: live harness references Python' >&2
	exit 1
fi
if grep -Eq '(^|[[:space:];|])([./A-Za-z0-9_-]+/)?captures?(/|[[:space:]]|$)|ROOT/.+\.raw' "$LIVE"; then
	echo 'task25: live harness targets repository capture artifacts' >&2
	exit 1
fi

grep -Fq 'mktemp -d "${TMPDIR:-/tmp}/sp7-cbridge-qualification.' "$LIVE"
grep -Fq 'REPEATS=${CBQ_REPEATS:-3}' "$LIVE"
grep -Fq 'DURATION=${CBQ_DURATION:-60}' "$LIVE"
grep -Fq 'owners_are_bridge_only' "$LIVE"
grep -Fq 'service_cgroup' "$LIVE"
grep -Fq 'owner_is_service_member' "$LIVE"
grep -Fq 'systemctl --user stop "$UNIT"' "$LIVE"
grep -Fq 'systemctl --user restart "$UNIT"' "$LIVE"
grep -Fq 'ffprobe' "$LIVE"
grep -Fq 'signalstats' "$LIVE"
grep -Fq 'run_case front yuyv' "$LIVE"
grep -Fq 'run_case rear yuyv' "$LIVE"
grep -Fq 'run_case front mjpeg' "$LIVE"
grep -Fq 'run_case rear mjpeg' "$LIVE"
grep -Fq 'OPEN_CLOSE_CYCLES=${CBQ_OPEN_CLOSE_CYCLES:-20}' "$LIVE"
grep -Fq 'SWITCH_CYCLES=${CBQ_SWITCH_CYCLES:-20}' "$LIVE"

printf '%s\n' 'task25-cbridge-live-static: PASS'
