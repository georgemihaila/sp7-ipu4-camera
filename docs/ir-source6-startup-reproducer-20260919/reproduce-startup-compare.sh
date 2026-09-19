#!/usr/bin/env bash
set -Eeuo pipefail

# Explicit hardware action. The hash gates prevent accidentally loading the
# bundled module or using a different qualifier binary than the recorded run.

if [[ ${RUN_HARDWARE:-0} != 1 ]]; then
	printf '%s\n' 'refusing hardware access; set RUN_HARDWARE=1 explicitly' >&2
	exit 2
fi

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd -- "$SCRIPT_DIR/../.." && pwd)
MODULE=${OV7251_QUALIFY_MODULE:?set OV7251_QUALIFY_MODULE to the candidate .ko}
OUT_DIR=${OV7251_OUTPUT_DIR:?set OV7251_OUTPUT_DIR to a new artifact directory}
QUALIFIER=$REPO_DIR/cbridge/ir-hardware-qualification
EXPECTED_MODULE=916c5e8d494663e0ccb3ccb071c8e84198bb4a24c345d03a90367ded77d5afc3
EXPECTED_QUALIFIER=30a299e22ca6a8f87121295e1d7c41039d2fae1c5190e7522ec7ac963399f8e1

[[ -r $MODULE ]] || { printf 'missing candidate: %s\n' "$MODULE" >&2; exit 1; }
[[ -x $QUALIFIER ]] || { printf 'missing qualifier: %s\n' "$QUALIFIER" >&2; exit 1; }

module_hash=$(sha256sum "$MODULE" | awk '{print $1}')
qualifier_hash=$(sha256sum "$QUALIFIER" | awk '{print $1}')
[[ $module_hash == "$EXPECTED_MODULE" ]] || {
	printf 'candidate hash mismatch: %s\n' "$module_hash" >&2
	exit 1
}
[[ $qualifier_hash == "$EXPECTED_QUALIFIER" ]] || {
	printf 'qualifier hash mismatch: %s\n' "$qualifier_hash" >&2
	exit 1
}

exec env \
	OV7251_QUALIFY_MODULE="$MODULE" \
	OV7251_QUALIFY_DURATION=600 \
	OV7251_QUALIFY_CYCLES=20 \
	OV7251_QUALIFY_CYCLE_FRAMES=5 \
	OV7251_QUALIFY_STARTUP_COMPARE=1 \
	OV7251_QUALIFY_MAX_STARTS=10 \
	OV7251_QUALIFY_DISCARD_ERROR_BUFFERS=0 \
	OV7251_OUTPUT_DIR="$OUT_DIR" \
	"$REPO_DIR/scripts/ir/qualify-persistent.sh"
