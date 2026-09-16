#!/bin/sh
# Install the Surface Pro 7 IPU4P modules from this source checkout.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

fail() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

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

printf 'Installing build tools for kernel %s...\n' "$KREL"
dnf -y install \
	ca-certificates curl dnf-plugins-core elfutils-libelf-devel gcc git make \
	msitools openssl-devel perl python3 bc dwarves flex bison kmod util-linux \
	libcamera-gstreamer akmod-v4l2loopback v4l2loopback v4l-utils

KDIR=${KDIR:-/lib/modules/$KREL/build}
kernel_tree_release() {
	if [ -f "$1/include/generated/utsrelease.h" ]; then
		sed -n 's/^#define UTS_RELEASE "\(.*\)"$/\1/p' \
			"$1/include/generated/utsrelease.h"
	elif [ -f "$1/include/config/kernel.release" ]; then
		cat "$1/include/config/kernel.release"
	fi
}

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

printf '%s\n' 'Installing verified modules and firmware...'
FIRMWARE="$FIRMWARE_PATH" KREL="$KREL" "$ROOT/scripts/install-modules.sh"

printf '%s\n' 'Installing named Surface Camera endpoints...'
if [ -n "${SUDO_USER:-}" ] && TARGET_UID=$(id -u "$SUDO_USER" 2>/dev/null) && \
	[ -S "/run/user/$TARGET_UID/bus" ]; then
	"$ROOT/scripts/setup-camera-bridge.sh"
else
	printf '%s\n' 'No active desktop user session was found; run sudo ./scripts/setup-camera-bridge.sh after logging in.'
fi

cat <<EOF

Installation complete for kernel $KREL. Surface Camera (front) and Surface Camera (back)
will be available to V4L2 applications after the desktop user service starts.
Reboot to load the modules. Secure Boot may require signing them with a key
trusted by this system before they can load.
EOF
