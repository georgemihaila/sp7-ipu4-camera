#!/bin/sh
# Static contract checks for the Phase 3 native PipeWire validator/policy.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VALIDATOR="$ROOT/tests/native-pipewire-validation.sh"
POLICY="$ROOT/wireplumber/60-sp7-ipu4-native.conf"
DOC="$ROOT/docs/native-pipewire.md"

test -x "$VALIDATOR"
test -f "$POLICY"
test -f "$DOC"
sh -n "$VALIDATOR"

grep -Fq 'pw-dump' "$VALIDATOR"
grep -Fq 'jq' "$VALIDATOR"
grep -Fq 'media.class' "$VALIDATOR"
grep -Fq 'Video/Source' "$VALIDATOR"
grep -Fq 'api.libcamera' "$VALIDATOR"
grep -Fq 'target-object=' "$VALIDATOR"
grep -Fq 'pipewiresrc' "$VALIDATOR"
grep -Fq 'num-buffers=' "$VALIDATOR"
grep -Fq 'video/x-raw' "$VALIDATOR"
grep -Fq 'framerate=' "$VALIDATOR"
grep -Fq 'multifilesink' "$VALIDATOR"
grep -Fq 'cksum' "$VALIDATOR"
grep -Fq 'SKIP' "$VALIDATOR"
grep -Fq 'FAIL' "$VALIDATOR"

# The validator may only inspect the graph and consume a source. These are
# forbidden mutation paths, including the old bridge/loopback stack.
! grep -Eiq 'systemctl|modprobe|pw-link|pw-cli[[:space:]]+(create|destroy|set)|v4l2loopback|cbridge' "$VALIDATOR"

grep -Fq 'monitor.v4l2.rules' "$POLICY"
grep -Fq 'device.name = "~v4l2_device.*intel-ipu60.*"' "$POLICY"
grep -Fq 'device.disabled = true' "$POLICY"
grep -Fq 'monitor.libcamera = enabled' "$POLICY"
! grep -Fq 'device.api = "libcamera"' "$POLICY"

grep -Fq 'Activation' "$DOC"
grep -Fq 'Rollback' "$DOC"
grep -Fq 'known black' "$DOC"
grep -Fq 'unqualified' "$DOC"
grep -Fq 'portal' "$DOC"

echo 'PASS [native-pipewire-static] dynamic native node discovery, bounded preview, image checks, and optional policy are present'
