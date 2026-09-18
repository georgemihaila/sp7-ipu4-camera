#!/bin/sh
# Build the pinned INT3472 OV7251 vdda experiment without installing it.
# The output is temporary by default and is never copied into /lib/modules.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KDIR=${KDIR:-/lib/modules/$(uname -r)/build}
KREL=${KREL:-$(uname -r)}
OUT=${OUT:-${TMPDIR:-/tmp}/sp7-int3472-vdda-build}
SOURCE_BASE=https://raw.githubusercontent.com/linux-surface/kernel/57d61aff0b53b089227f5a794363fec829114fc/drivers/platform/x86/intel/int3472
PATCH_FILE=$ROOT/patches/int3472-ov7251-vdda.patch

die() { printf '%s\n' "$1" >&2; exit 2; }
[ -f "$KDIR/Makefile" ] && [ -f "$KDIR/.config" ] || die "prepared kernel tree not found: $KDIR"
[ -f "$PATCH_FILE" ] || die "patch file not found: $PATCH_FILE"
[ "$($ROOT/scripts/kernel-release.sh "$KDIR")" = "$KREL" ] || die "KDIR release does not match KREL=$KREL"

mkdir -p "$OUT"
fetch_source() {
  name=$1
  expected=$2
  curl -fsSL "$SOURCE_BASE/$name" -o "$OUT/$name"
  printf '%s  %s\n' "$expected" "$OUT/$name" | sha256sum -c -
}
fetch_source discrete.c 4a125466ec4cdf5827c40c8bf5375f84509b92336f44936f7b390e3f27493c7e
fetch_source discrete_quirks.c 7dbcf534946cd836ddcfb0ecb780ab3666edf5caf0755cd59ae7d9eefe51fd6f
fetch_source clk_and_regulator.c cd8314bc053982136205202a194c66b4dcffe9b8bdc21fbaee5b5afd34b3378f
fetch_source led.c 88615208e4292b81c36c8ece1475528d83426ef0897951abbdfeafc98ad4b3dd
patch --batch --forward "$OUT/discrete.c" < "$PATCH_FILE"

printf '%s\n' 'obj-m := intel_skl_int3472_discrete.o' \
  'intel_skl_int3472_discrete-y := discrete.o discrete_quirks.o clk_and_regulator.o led.o' > "$OUT/Makefile"
make -C "$KDIR" M="$OUT" EXTERNAL_BUILD=1 modules

MODULE="$OUT/intel_skl_int3472_discrete.ko"
[ -f "$MODULE" ] || die "expected module was not built: $MODULE"
vermagic=$(modinfo -F vermagic "$MODULE")
case "$vermagic" in
  "$KREL"*) : ;;
  *) die "vermagic does not match $KREL: $vermagic" ;;
esac

printf 'module=%s\n' "$MODULE"
sha256sum "$MODULE"
printf 'vermagic=%s\n' "$vermagic"
printf '%s\n' 'build complete; no install, unload, load, rebind, depmod, or reboot was performed'
