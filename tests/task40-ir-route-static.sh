#!/bin/sh
# Hardware-independent checks for the separate source-6 route lifecycle.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$ROOT/cbridge/ir-route.c"
INSTALL="$ROOT/scripts/ir/install-route-lifecycle.sh"
UNIT="$ROOT/systemd/system/sp7-camera-howdy-route.service"

grep -Fq 'MEDIA_IOC_ENUM_ENTITIES' "$SOURCE"
grep -Fq 'MEDIA_IOC_ENUM_LINKS' "$SOURCE"
grep -Fq 'MEDIA_IOC_SETUP_LINK' "$SOURCE"
grep -Fq 'ov7251' "$SOURCE"
grep -Fq 'Intel IPU4 CSI-2 1' "$SOURCE"
grep -Fq 'Intel IPU4 CSI-2 1 capture 0' "$SOURCE"
grep -Fq 'IR_ROUTE_DEFAULT_STATE' "$SOURCE"
grep -Fq 'Restore in reverse pipeline order' "$SOURCE"
grep -Fq 'Authentication never calls' "$SOURCE"
! grep -Fq '/dev/video62' "$SOURCE"
! grep -Fq 'modprobe' "$SOURCE"
! grep -Fq 'systemctl' "$SOURCE"

grep -Fq 'ExecStart=/usr/local/libexec/sp7-camera-ir-route prepare' "$UNIT"
grep -Fq 'ExecStop=/usr/local/libexec/sp7-camera-ir-route restore' "$UNIT"
grep -Fq 'RemainAfterExit=yes' "$UNIT"
grep -Fq 'disabled and stopped' "$INSTALL"
grep -Fq 'Enable only after' "$INSTALL"

bash -n "$INSTALL"
tmpdir=$(mktemp -d /var/tmp/sp7-ir-route-static.XXXXXX)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
	-o "$tmpdir/sp7-camera-ir-route" "$SOURCE"
"$tmpdir/sp7-camera-ir-route" --help >/dev/null
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
	-o "$tmpdir/ir-route-mock-test" "$ROOT/tests/ir-route-mock-test.c"
"$tmpdir/ir-route-mock-test" >/dev/null

printf '%s\n' 'task40-ir-route-static: PASS'
