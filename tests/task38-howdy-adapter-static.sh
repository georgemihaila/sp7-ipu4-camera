#!/bin/sh
# Offline/static checks for the pinned Howdy sp7_ir recorder contract.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PATCH="$ROOT/scripts/ir/howdy-sp7_ir.patch"
CONFIG="$ROOT/scripts/ir/howdy-sp7-ir-config.ini"
READER="$ROOT/scripts/ir/sp7_ir_reader.py"
PROTOCOL="$ROOT/scripts/ir/sp7_ir_protocol.py"
TEST="$ROOT/tests/howdy-sp7-ir-adapter-test.py"
DOC="$ROOT/docs/howdy-sp7-ir-runtime-20260919.md"

grep -Fq 'd3ab99382f88f043d15f15c1450ab69433892a1c' "$ROOT/scripts/ir/howdy-revision.txt"
grep -Fq 'recording_plugin = sp7_ir' "$CONFIG"
grep -Fq 'timeout = 5' "$CONFIG"
grep -Fq 'dark_threshold = 60' "$CONFIG"
grep -Fq 'MAX_FRAMES = 12' "$PROTOCOL"
grep -Fq 'CAPTURE_BUDGET_SECONDS = 3.0' "$PROTOCOL"
grep -Fq 'deadline=attempt_deadline' "$READER"
grep -Fq 'return False' "$READER"
grep -Fq 'Sp7IrExhausted' "$PATCH"
grep -Fq 'attempt_deadline = attempt_start + timeout' "$PATCH"
grep -Fq 'dark_threshold = config.getfloat' "$PATCH"
grep -Fq 'exit(14)' "$PATCH"
grep -Fq 'exit(11)' "$PATCH"
grep -Fq 'SP7IRF01' "$DOC"
grep -Fq 'clean 12-frame exhaustion is a no-match' "$DOC"
grep -Fq 'three-second capture budget' "$DOC"
grep -Fq 'dark-frame rejection' "$DOC"

PY_CACHE=$(mktemp -d /var/tmp/sp7-howdy-adapter-pycompile.XXXXXX)
trap 'rm -rf "$PY_CACHE"' EXIT
PYTHONPYCACHEPREFIX="$PY_CACHE" python3 -m py_compile \
	"$READER" "$PROTOCOL" "$TEST"
python3 "$TEST"
printf '%s\n' 'task38-howdy-adapter-static: PASS'
