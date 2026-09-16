#!/bin/sh
# Remove the named Surface Camera bridge and restore RPM Fusion's default OBS loopback.
set -eu

fail() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

[ "$(id -u)" -eq 0 ] || fail 'run as root: sudo ./scripts/remove-camera-bridge.sh'
TARGET_USER=${SUDO_USER:-}
[ -n "$TARGET_USER" ] && [ "$TARGET_USER" != root ] || fail 'run this through sudo from the desktop user account'
TARGET_UID=$(id -u "$TARGET_USER")
TARGET_HOME=$(getent passwd "$TARGET_USER" | cut -d: -f6)
RUNTIME_DIR=/run/user/$TARGET_UID

if [ -S "$RUNTIME_DIR/bus" ]; then
	runuser -u "$TARGET_USER" -- env \
		XDG_RUNTIME_DIR="$RUNTIME_DIR" \
		DBUS_SESSION_BUS_ADDRESS="unix:path=$RUNTIME_DIR/bus" \
		systemctl --user disable --now sp7-camera-bridge.service 2>/dev/null || true
fi
if command -v fuser >/dev/null 2>&1; then
	for dev in /dev/video55 /dev/video60 /dev/video61; do
		if [ -e "$dev" ] && fuser -s "$dev"; then
			if [ -S "$RUNTIME_DIR/bus" ]; then
				runuser -u "$TARGET_USER" -- env \
					XDG_RUNTIME_DIR="$RUNTIME_DIR" \
					DBUS_SESSION_BUS_ADDRESS="unix:path=$RUNTIME_DIR/bus" \
					systemctl --user enable --now sp7-camera-bridge.service 2>/dev/null || true
			fi
			fail "a loopback camera is still open at $dev; close apps using these devices, then rerun removal"
		fi
	done
fi

rm -f "$TARGET_HOME/.config/systemd/user/sp7-camera-bridge.service"
rm -f "$TARGET_HOME/.config/systemd/user/sp7-zoom-camera-bridge.service"
rm -f /usr/local/libexec/sp7-camera-bridge
rm -f /usr/local/libexec/sp7-zoom-camera-bridge
rm -f /etc/modules-load.d/sp7-camera-bridge.conf
rm -f /etc/modules-load.d/sp7-zoom-camera-bridge.conf
rm -f /etc/wireplumber/wireplumber.conf.d/50-sp7-ipu4.conf
rm -f /etc/modprobe.d/98-v4l2loopback.conf
modprobe -r v4l2loopback 2>/dev/null || true
if [ -S "$RUNTIME_DIR/bus" ]; then
	runuser -u "$TARGET_USER" -- env \
		XDG_RUNTIME_DIR="$RUNTIME_DIR" \
		DBUS_SESSION_BUS_ADDRESS="unix:path=$RUNTIME_DIR/bus" \
		systemctl --user daemon-reload
fi
printf 'Removed the Surface Camera bridge. RPM Fusion packages remain installed, and its OBS default is restored on the next module load.\n'
