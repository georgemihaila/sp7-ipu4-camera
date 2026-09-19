#!/bin/sh
# Install the Surface Pro 7 IPU4P modules from this source checkout.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
INSTALL_MODE=${INSTALL_MODE:-full}
. "$ROOT/scripts/kernel-release.sh"

fail() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

usage() {
	cat <<'EOF'
usage: sudo ./install.sh [--full|--driver-only]

The default --full mode builds and installs the IPU4P driver and configures
the named Surface Camera bridge when a desktop session is available.
--driver-only installs only the IPU4P modules and firmware. It does not
install bridge packages, write bridge configuration, or enable user services.
For a prebuilt release archive, use scripts/install-modules.sh directly; it
does not install compiler or development packages.
EOF
}

while [ "$#" -gt 0 ]; do
	case $1 in
		--full) INSTALL_MODE=full ;;
		--driver-only) INSTALL_MODE=driver-only ;;
		--help|-h) usage; exit 0 ;;
		*) fail "unknown option: $1" ;;
	esac
	shift
done

case $INSTALL_MODE in
	full|driver-only) ;;
	*) fail "invalid INSTALL_MODE: $INSTALL_MODE (expected full or driver-only)" ;;
esac

[ "$(id -u)" -eq 0 ] || fail 'run this installer as root: sudo ./install.sh'

if [ ! -r /etc/os-release ]; then
	fail 'cannot identify this Linux distribution (/etc/os-release is missing)'
fi
. /etc/os-release
[ "${ID:-}" = fedora ] || fail 'automatic installation currently supports Fedora only'
[ "$(uname -m)" = x86_64 ] || fail 'automatic installation currently supports x86_64 only'

KREL=$(uname -r)
case $KREL in
	''|*[!A-Za-z0-9._+-]*) fail "unexpected running kernel release: $KREL" ;;
esac
case $KREL in
	*.surface.*) ;;
	*) fail "running kernel $KREL is not a linux-surface kernel" ;;
esac

if [ "${FIRMWARE+x}" = x ] && [ ! -s "$FIRMWARE" ]; then
	fail "FIRMWARE does not name a non-empty file: ${FIRMWARE:-<empty>}"
fi

DRIVER_PACKAGES='ca-certificates curl dnf-plugins-core kmod util-linux'
BUILD_PACKAGES='elfutils-libelf-devel gcc git make openssl-devel perl python3 bc dwarves flex bison'
BRIDGE_PACKAGES='libcamera-gstreamer gstreamer1-plugins-good akmod-v4l2loopback v4l2loopback v4l-utils'
BRIDGE_BUILD_PACKAGES='gcc make pkgconf-pkg-config gstreamer1-devel glib2-devel'

WORKDIR=$(mktemp -d)
cleanup() {
	status=$?
	trap - EXIT
	rm -rf -- "$WORKDIR"
	exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

printf 'Installing driver dependencies for kernel %s...\n' "$KREL"
dnf -y install $DRIVER_PACKAGES $BUILD_PACKAGES
if [ "$INSTALL_MODE" = full ]; then
	printf '%s\n' 'Installing named-camera runtime dependencies...'
	dnf -y install $BRIDGE_PACKAGES $BRIDGE_BUILD_PACKAGES
fi

KDIR=${KDIR:-/lib/modules/$KREL/build}

KDIR_RELEASE=$(kernel_tree_release "$KDIR")
if [ ! -f "$KDIR/Makefile" ] || [ ! -f "$KDIR/.config" ] || [ "$KDIR_RELEASE" != "$KREL" ]; then
	SURFACE_REPO_FILE=${SURFACE_REPO_FILE:-/etc/yum.repos.d/linux-surface.repo}
	SURFACE_REPO_DIR=$(dirname -- "$SURFACE_REPO_FILE")
	if [ -e "$SURFACE_REPO_FILE" ]; then
		[ -f "$SURFACE_REPO_FILE" ] || fail "linux-surface repo path is not a regular file: $SURFACE_REPO_FILE"
	else
		mkdir -p "$SURFACE_REPO_DIR"
		curl -fsSL --retry 3 https://pkg.surfacelinux.com/fedora/linux-surface.repo \
			-o "$WORKDIR/linux-surface.repo"
		install -m 0644 "$WORKDIR/linux-surface.repo" "$SURFACE_REPO_FILE"
	fi
	rpm --import https://raw.githubusercontent.com/linux-surface/linux-surface/master/pkg/keys/surface.asc
	if ! dnf -y install "kernel-surface-devel-$KREL"; then
		fail "could not install kernel-surface-devel-$KREL; verify this kernel is available from the linux-surface Fedora repository"
	fi

	KDIR_RELEASE=$(kernel_tree_release "$KDIR")
	if [ ! -f "$KDIR/Makefile" ] || [ ! -f "$KDIR/.config" ] || [ "$KDIR_RELEASE" != "$KREL" ]; then
		KDIR=/usr/src/kernels/$KREL
	fi
fi
KDIR_RELEASE=$(kernel_tree_release "$KDIR")
[ -f "$KDIR/Makefile" ] && [ -f "$KDIR/.config" ] && [ "$KDIR_RELEASE" = "$KREL" ] || \
	fail "no prepared kernel build tree for $KREL; expected /lib/modules/$KREL/build or /usr/src/kernels/$KREL"

# Firmware is not shipped in this repository. Prefer a caller-supplied copy,
# then the standard installed location, and otherwise extract it from the
# official Microsoft Surface Pro 7 driver package.
FIRMWARE_PATH=${FIRMWARE:-}
if [ -n "$FIRMWARE_PATH" ]; then
	[ -f "$FIRMWARE_PATH" ] || fail "FIRMWARE does not name a file: $FIRMWARE_PATH"
elif [ -f /lib/firmware/ipu4p_cpd.bin ]; then
	FIRMWARE_PATH=/lib/firmware/ipu4p_cpd.bin
elif [ -f /usr/lib/firmware/ipu4p_cpd.bin ]; then
	FIRMWARE_PATH=/usr/lib/firmware/ipu4p_cpd.bin
else
	MSI_URL=https://download.microsoft.com/download/b7ea0d32-93fb-4733-b78c-4263b314dbf4/SurfacePro7_Win11_22621_25.090.3489.0.msi
	MSI_PATH="$WORKDIR/SurfacePro7_Win11_22621_25.090.3489.0.msi"
	EXTRACT_DIR="$WORKDIR/microsoft-driver-package"
	mkdir -p "$EXTRACT_DIR"
	# Install the extraction-only tool only when no usable firmware was found.
	dnf -y install msitools
	printf '%s\n' 'CPD firmware is missing; downloading the official Microsoft Surface Pro 7 driver package (about 678 MB)...'
	curl -fL --retry 3 "$MSI_URL" -o "$MSI_PATH"
	msiextract -C "$EXTRACT_DIR" "$MSI_PATH"
	find "$EXTRACT_DIR" -type f -name cpd_component_signed.bin -print > "$WORKDIR/cpd-candidates"
	CANDIDATE_COUNT=$(wc -l < "$WORKDIR/cpd-candidates")
	[ "$CANDIDATE_COUNT" -eq 1 ] || \
		fail "expected one cpd_component_signed.bin in Microsoft's package; found $CANDIDATE_COUNT"
	IFS= read -r FIRMWARE_PATH < "$WORKDIR/cpd-candidates"
	[ -n "$FIRMWARE_PATH" ] && [ -s "$FIRMWARE_PATH" ] || \
		fail 'Microsoft driver package did not contain cpd_component_signed.bin'
fi
[ -s "$FIRMWARE_PATH" ] || fail "firmware file is missing or empty: $FIRMWARE_PATH"

printf 'Building IPU4P modules for %s...\n' "$KREL"
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')
case $JOBS in ''|*[!0-9]*|0) JOBS=2 ;; esac
BUILD_UID=${SUDO_UID:-$(stat -c '%u' "$ROOT")}
if [ "$BUILD_UID" != 0 ]; then
	BUILD_USER=${SUDO_USER:-$(getent passwd "$BUILD_UID" | cut -d: -f1)}
	[ -n "$BUILD_USER" ] || fail "cannot identify the non-root owner for building modules (uid $BUILD_UID)"
	command -v runuser >/dev/null 2>&1 || fail 'runuser is required to build without creating root-owned files in the checkout'
	runuser -u "$BUILD_USER" -- test -w "$ROOT" || \
		fail "checkout is not writable by build user $BUILD_USER: $ROOT"
	runuser -u "$BUILD_USER" -- env PATH="$PATH" KDIR="$KDIR" KREL="$KREL" \
		"$ROOT/scripts/build-modules.sh" "-j$JOBS"
else
	KDIR="$KDIR" KREL="$KREL" "$ROOT/scripts/build-modules.sh" "-j$JOBS"
fi

if [ "$INSTALL_MODE" = full ]; then
	printf '%s\n' 'Building the C camera bridge...'
	if [ "$BUILD_UID" != 0 ]; then
		runuser -u "$BUILD_USER" -- env PATH="$PATH" \
			make -C "$ROOT/cbridge" all
	else
		make -C "$ROOT/cbridge" all
	fi
fi

printf '%s\n' 'Installing verified modules and firmware...'
FIRMWARE="$FIRMWARE_PATH" KREL="$KREL" "$ROOT/scripts/install-modules.sh"

if [ "$INSTALL_MODE" = full ]; then
	command -v gst-inspect-1.0 >/dev/null 2>&1 || fail 'gst-inspect-1.0 is required to validate GStreamer runtime elements'
	for element in libcamerasrc videotestsrc videoconvert videoscale jpegenc jpegparse v4l2sink filesink; do
		gst-inspect-1.0 "$element" >/dev/null 2>&1 || \
			fail "required GStreamer element is unavailable: $element (check libcamera-gstreamer and gstreamer1-plugins-good)"
	done
	printf '%s\n' 'Installing named Surface Camera endpoints...'
	if [ -n "${SUDO_USER:-}" ] && TARGET_UID=$(id -u "$SUDO_USER" 2>/dev/null) && \
		[ -S "/run/user/$TARGET_UID/bus" ]; then
		"$ROOT/scripts/setup-camera-bridge.sh"
	else
		printf '%s\n' 'No active desktop user session was found; run sudo ./scripts/setup-camera-bridge.sh after logging in.'
	fi
else
	printf '%s\n' 'Driver-only mode selected; named-camera packages and bridge configuration were skipped.'
fi

cat <<EOF

Installation complete for kernel $KREL.
EOF
if [ "$INSTALL_MODE" = full ]; then
	cat <<EOF
Surface Camera (front), Surface Camera (back), and the opt-in Surface Camera
(IR) endpoint will be available to V4L2 applications after setup. The IR
producer is not started by the desktop user service.
EOF
else
	cat <<EOF
Only the driver and firmware were installed. Use --full later, or install the
named-camera dependencies and run scripts/setup-camera-bridge.sh separately.
EOF
fi
cat <<EOF
Reboot to load the modules. Secure Boot may require signing them with a key
trusted by this system before they can load.
EOF
