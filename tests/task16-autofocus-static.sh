#!/bin/sh
# Guard the rear lens-actuator binding fix and its module packaging contract.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DRIVER="$ROOT/linux-6.19.8/drivers/media/i2c/dw9719.c"
BUILD="$ROOT/scripts/build-modules.sh"
INSTALL="$ROOT/scripts/install-modules.sh"
UNINSTALL="$ROOT/scripts/uninstall-modules.sh"
PACKAGE="$ROOT/scripts/package-release.sh"
DOC="$ROOT/docs/task8-camera-contract.md"
README="$ROOT/README.md"
AUTOFOCUS="$ROOT/autofocus-rear.sh"

[ -f "$DRIVER" ]
grep -Fq 'i2c_get_match_data(client)' "$DRIVER"
grep -Fq 'V4L2_CID_FOCUS_ABSOLUTE' "$DRIVER"
grep -Fq 'MODULE_DEVICE_TABLE(i2c, dw9719_i2c_ids)' "$DRIVER"
grep -Fq '.id_table = dw9719_i2c_ids' "$DRIVER"
grep -Fq 'v4l2_device_register_subdev_nodes(dw9719->sd.v4l2_dev)' "$DRIVER"
for match in 'dw9718s' 'dw9719' 'dw9761' 'dw9800k'; do
	grep -Fq ".name = \"$match\"" "$DRIVER"
done
grep -Fq 'make -C "$KDIR" M="$VCM_SRC" modules' "$BUILD"
grep -Fq 'i2c:dw9719' "$BUILD"
grep -Fq 'dw9719.ko' "$INSTALL"
grep -Fq 'dw9719.ko' "$UNINSTALL"
grep -Fq 'drivers/media/i2c/dw9719.ko|dw9719.ko' "$PACKAGE"
grep -Fiq 'dw9719' "$DOC"
grep -Fq 'test-capture.sh' "$AUTOFOCUS"
grep -Fq 'focus_absolute' "$AUTOFOCUS"
grep -Fq '/^- entity .*: dw9719/' "$AUTOFOCUS"
grep -Fq 'runuser -u "$WP_USER"' "$AUTOFOCUS"
grep -Fq 'XDG_RUNTIME_DIR="$WP_RUNTIME_DIR"' "$AUTOFOCUS"
grep -Fq 'DBUS_SESSION_BUS_ADDRESS="$WP_DBUS_ADDRESS"' "$AUTOFOCUS"
grep -Fq 'is-active --quiet wireplumber.service' "$AUTOFOCUS"
grep -Fq 'stop wireplumber.service' "$AUTOFOCUS"
grep -Fq 'start wireplumber.service' "$AUTOFOCUS"
grep -Fq 'if (( WIREPLUMBER_STOPPED ))' "$AUTOFOCUS"
grep -Fq 'one-shot' "$README"

echo 'task16-autofocus-static: PASS'
