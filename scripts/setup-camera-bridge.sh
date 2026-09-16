#!/bin/sh
# Install the named Surface Camera endpoints for V4L2 applications.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fail() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

[ "$(id -u)" -eq 0 ] || fail 'run as root: sudo ./scripts/setup-camera-bridge.sh'
TARGET_USER=${SUDO_USER:-}
[ -n "$TARGET_USER" ] || fail 'run this through sudo from the desktop user account'
[ "$TARGET_USER" != root ] || fail 'the bridge must run in a logged-in desktop user session'
TARGET_UID=$(id -u "$TARGET_USER")
TARGET_GID=$(id -g "$TARGET_USER")
TARGET_HOME=$(getent passwd "$TARGET_USER" | cut -d: -f6)
[ -n "$TARGET_HOME" ] && [ -d "$TARGET_HOME" ] || fail "cannot find the home directory for $TARGET_USER"
RUNTIME_DIR=/run/user/$TARGET_UID
[ -S "$RUNTIME_DIR/bus" ] || fail "no active user session bus at $RUNTIME_DIR/bus; log in as $TARGET_USER and rerun setup"

for package in libcamera-gstreamer gstreamer1-plugins-good akmod-v4l2loopback v4l2loopback; do
	rpm -q "$package" >/dev/null 2>&1 || fail "missing package $package; install it with DNF first (akmod-v4l2loopback is from RPM Fusion Free)"
done
command -v v4l2-ctl >/dev/null 2>&1 || fail 'v4l2-ctl is required to verify the virtual camera devices'
command -v gst-launch-1.0 >/dev/null 2>&1 || fail 'GStreamer tools are required'
command -v gst-inspect-1.0 >/dev/null 2>&1 || fail 'gst-inspect-1.0 is required to validate GStreamer plugins'
command -v runuser >/dev/null 2>&1 || fail 'runuser is required to configure the target user session'

for element in libcamerasrc videotestsrc videoconvert videoscale jpegenc jpegparse v4l2sink filesink; do
	gst-inspect-1.0 "$element" >/dev/null 2>&1 || \
		fail "required GStreamer element is unavailable: $element (install libcamera-gstreamer and gstreamer1-plugins-good)"
done

KREL=$(uname -r)
if ! modinfo -k "$KREL" v4l2loopback >/dev/null 2>&1; then
	command -v akmods >/dev/null 2>&1 || fail "v4l2loopback is not built for $KREL and akmods is unavailable"
	akmods --force --kernels "$KREL"
	depmod "$KREL"
	modinfo -k "$KREL" v4l2loopback >/dev/null 2>&1 || fail "could not build v4l2loopback for $KREL"
fi

OPTIONS=/etc/modprobe.d/98-v4l2loopback.conf
MODULES=/etc/modules-load.d/sp7-camera-bridge.conf
USER_UNIT_DIR=$TARGET_HOME/.config/systemd/user
WIREPLUMBER_DIR=/etc/wireplumber/wireplumber.conf.d
WIREPLUMBER_OPTIONS=$WIREPLUMBER_DIR/50-sp7-ipu4.conf

# Migrate the earlier Zoom-specific unit before checking loopback users. Its
# idle producers would otherwise make the devices look busy during reload.
if [ -e "$USER_UNIT_DIR/sp7-zoom-camera-bridge.service" ]; then
	runuser -u "$TARGET_USER" -- env \
		XDG_RUNTIME_DIR="$RUNTIME_DIR" \
		DBUS_SESSION_BUS_ADDRESS="unix:path=$RUNTIME_DIR/bus" \
		systemctl --user disable --now sp7-zoom-camera-bridge.service 2>/dev/null || true
	rm -f "$USER_UNIT_DIR/sp7-zoom-camera-bridge.service"
fi
if [ -e "$OPTIONS" ] && ! cmp -s "$ROOT/modprobe.d/98-v4l2loopback.conf" "$OPTIONS"; then
	fail "$OPTIONS already exists with different contents; review it before replacing"
fi

for dev in /dev/video55 /dev/video60 /dev/video61; do
	if [ -e "$dev" ] && ! v4l2-ctl --device "$dev" --all 2>/dev/null | grep -Eq 'Card type.*(OBS Virtual Camera|Surface Camera \(front\)|Surface Camera \(back\)|Surface Pro 7 Front Camera|Surface Pro 7 Rear Camera)'; then
		fail "$dev is already assigned to another video device"
	fi
done

if lsmod | awk '$1 == "v4l2loopback" { found = 1 } END { exit !found }'; then
	if command -v fuser >/dev/null 2>&1; then
		for dev in /dev/video55 /dev/video60 /dev/video61; do
			if [ -e "$dev" ] && fuser -s "$dev"; then
				fail "a loopback camera is open at $dev; close the app using it and rerun setup"
			fi
		done
	fi
	modprobe -r v4l2loopback || fail 'could not reload v4l2loopback; close any app using a loopback camera and retry'
fi

install -D -m 0644 "$ROOT/modprobe.d/98-v4l2loopback.conf" "$OPTIONS"
install -D -m 0644 /dev/null "$MODULES"
printf '%s\n' v4l2loopback > "$MODULES"
modprobe v4l2loopback

for pair in \
	'/dev/video55|OBS Virtual Camera' \
	'/dev/video60|Surface Camera (front)' \
	'/dev/video61|Surface Camera (back)'; do
	dev=${pair%%|*}
	label=${pair#*|}
	v4l2-ctl --device "$dev" --all 2>/dev/null | grep -Fq "$label" || fail "expected label '$label' on $dev"
done

install -D -m 0755 "$ROOT/scripts/surface-camera-bridge.py" /usr/local/libexec/sp7-camera-bridge
install -d -m 0755 -o "$TARGET_UID" -g "$TARGET_GID" "$USER_UNIT_DIR"
install -m 0644 -o "$TARGET_UID" -g "$TARGET_GID" \
	"$ROOT/systemd/user/sp7-camera-bridge.service" \
	"$USER_UNIT_DIR/sp7-camera-bridge.service"
install -d -m 0755 "$WIREPLUMBER_DIR"
if [ -e "$WIREPLUMBER_OPTIONS" ] && ! cmp -s "$ROOT/wireplumber/50-sp7-ipu4.conf" "$WIREPLUMBER_OPTIONS"; then
	fail "$WIREPLUMBER_OPTIONS already exists with different contents; review it before replacing"
fi
install -m 0644 "$ROOT/wireplumber/50-sp7-ipu4.conf" "$WIREPLUMBER_OPTIONS"

runuser -u "$TARGET_USER" -- env \
	XDG_RUNTIME_DIR="$RUNTIME_DIR" \
	DBUS_SESSION_BUS_ADDRESS="unix:path=$RUNTIME_DIR/bus" \
	systemctl --user daemon-reload
runuser -u "$TARGET_USER" -- env \
	XDG_RUNTIME_DIR="$RUNTIME_DIR" \
	DBUS_SESSION_BUS_ADDRESS="unix:path=$RUNTIME_DIR/bus" \
	systemctl --user enable --now sp7-camera-bridge.service

# Apply the camera monitor rules to the current user session.  A restart is
# limited to WirePlumber; PipeWire itself and active audio streams remain up.
runuser -u "$TARGET_USER" -- env \
	XDG_RUNTIME_DIR="$RUNTIME_DIR" \
	DBUS_SESSION_BUS_ADDRESS="unix:path=$RUNTIME_DIR/bus" \
	systemctl --user restart wireplumber.service

printf 'Installed. Surface Camera (front) is /dev/video60 and Surface Camera (back) is /dev/video61.\n'
printf 'The existing OBS Virtual Camera remains on /dev/video55.\n'
