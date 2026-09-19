#!/bin/sh
# Prepare the isolated, non-PAM recognition environment.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
VENV=${SP7_AUTH_VENV:-$ROOT/.venv-auth}
REQUIREMENTS=$ROOT/scripts/ir/requirements-py314.txt

case $VENV in
	/*) ;;
	*) printf 'error: SP7_AUTH_VENV must be absolute: %s\n' "$VENV" >&2; exit 2 ;;
esac

umask 077
if [ ! -x "$VENV/bin/python" ]; then
	python3 -m venv "$VENV"
fi
PYTHON_VERSION=$($VENV/bin/python -c 'import sys; print("%d.%d" % sys.version_info[:2])')
[ "$PYTHON_VERSION" = 3.14 ] || {
	printf 'error: isolated environment requires Python 3.14, found %s\n' "$PYTHON_VERSION" >&2
	exit 2
}
$VENV/bin/python -m pip install --no-cache-dir -r "$REQUIREMENTS"
$VENV/bin/python - <<'PY'
import cv2
import dlib
import numpy

print("auth-recognition-environment: PASS")
print("numpy=" + numpy.__version__)
print("opencv=" + cv2.__version__)
print("dlib=" + dlib.__version__)
PY
printf 'Prepared isolated environment at %s\n' "$VENV"
