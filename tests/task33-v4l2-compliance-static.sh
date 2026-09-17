#!/bin/sh
# Static contract checks for the read-only Phase 5 V4L2 compliance gate.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VALIDATOR="$ROOT/tests/v4l2-compliance-validation.sh"

test -x "$VALIDATOR"
sh -n "$VALIDATOR"
grep -Fq '/sys/class/video4linux/video*' "$VALIDATOR"
grep -Fq 'readlink -f "$sysnode/device/driver"' "$VALIDATOR"
grep -Fq 'readlink -f "$sysnode/device/driver/module"' "$VALIDATOR"
grep -Fq 'v4l2-compliance --device="$node"' "$VALIDATOR"
grep -Fq 'v4l2-ctl --device="$node" --all' "$VALIDATOR"
grep -Fq 'v4l2-ctl --device="$node" --list-formats-ext' "$VALIDATOR"
grep -Fq 'modinfo "$module_name"' "$VALIDATOR"
grep -Fq 'FIRMWARE' "$VALIDATOR"
grep -Fq 'SKIP' "$VALIDATOR"
grep -Fq 'FAIL' "$VALIDATOR"
grep -Fq 'timeout --signal=TERM' "$VALIDATOR"

if grep -Eq '/dev/video[0-9]|/dev/media[0-9]|/dev/v4l-subdev[0-9]' "$VALIDATOR"; then
	echo "hard-coded media/video/sub-device minor found" >&2
	exit 1
fi

# The compliance gate must remain observational: no module, graph, service,
# or application-bridge state changes are allowed here.
if grep -Eiq '(^|[[:space:];])((sudo[[:space:]]+)?modprobe|rmmod|insmod|systemctl|pw-link|v4l2loopback|cbridge[[:space:]]+--(start|stop))' "$VALIDATOR"; then
	echo "mutating module/graph/service/bridge command found" >&2
	exit 1
fi

echo 'PASS [v4l2-compliance-static] dynamic sysfs ownership, capability/format/provenance recording, skip/fail semantics, and read-only contract are present'
