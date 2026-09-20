#!/bin/sh
# Regression checks for the installed OV7251 module defaults.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
INSTALL="$ROOT/scripts/install-modules.sh"
UNINSTALL="$ROOT/scripts/uninstall-modules.sh"
CONFIG="$ROOT/modprobe.d/99-sp7-ov7251.conf"

sh -n "$INSTALL" "$UNINSTALL"
grep -Fq 'options ov7251 experimental_strobe_output=1 strobe_diagnostics=1' "$CONFIG"
grep -Fq '99-sp7-ov7251.conf' "$INSTALL" "$UNINSTALL" "$ROOT/scripts/package-release.sh"

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
mockbin="$tmpdir/bin"
mkdir -p "$mockbin"
cat > "$mockbin/modinfo" <<'EOF'
#!/bin/sh
if [ "$1" = -k ] && [ "$3" = -n ] && [ "$4" = ipu_bridge ]; then
	exit 1
fi
[ "$1" = -F ] && [ "$2" = vermagic ] || exit 2
printf '%s SMP test\n' "$KREL"
EOF
cat > "$mockbin/depmod" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$mockbin/modinfo" "$mockbin/depmod"

source_root="$tmpdir/source"
mkdir -p "$source_root/linux-6.19.8/drivers/media/pci/intel" "$source_root/modules" "$source_root/modprobe.d"
printf '%s\n' 'linux-6.19.8/drivers/media/pci/intel/ipu-bridge.ko|ipu-bridge.ko' > \
	"$source_root/modules/ipu4p-camera.modules"
printf '%s\n' 'fake module payload' > \
	"$source_root/linux-6.19.8/drivers/media/pci/intel/ipu-bridge.ko"
cp "$CONFIG" "$source_root/modprobe.d/99-sp7-ov7251.conf"
printf '%s\n' 'firmware bytes' > "$tmpdir/firmware"

moddir="$tmpdir/modules"
modprobe_config="$tmpdir/etc/modprobe.d/99-sp7-ov7251.conf"
firmware_target="$tmpdir/firmware-target"
install_env="PATH=$mockbin:$PATH MODULE_SOURCE_ROOT=$source_root KREL=task37-test MODDIR=$moddir MODPROBE_CONFIG=$modprobe_config FIRMWARE=$tmpdir/firmware FIRMWARE_TARGET=$firmware_target"
env $install_env sh "$INSTALL"
[ -f "$modprobe_config" ]
cmp -s "$CONFIG" "$modprobe_config"
[ -f "$moddir/.ipu4p-camera-ov7251-modprobe" ]

# Reinstalling the same payload is idempotent.
env $install_env sh "$INSTALL"
env PATH="$mockbin:$PATH" KREL=task37-test MODDIR="$moddir" MODPROBE_CONFIG="$modprobe_config" sh "$UNINSTALL"
[ ! -e "$modprobe_config" ]
[ ! -e "$moddir/.ipu4p-camera-ov7251-modprobe" ]

# A user-modified configuration is never overwritten.
mkdir -p "$(dirname "$modprobe_config")"
printf '%s\n' 'options ov7251 user_selected_value=1' > "$modprobe_config"
if env $install_env sh "$INSTALL" >"$tmpdir/collision.log" 2>&1; then
	echo 'task37-ov7251-installer-defaults: modified config was overwritten' >&2
	exit 1
fi
grep -Fq 'refusing to overwrite existing OV7251 modprobe configuration' "$tmpdir/collision.log"
grep -Fq 'user_selected_value=1' "$modprobe_config"

echo 'task37-ov7251-installer-defaults: PASS'
