#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
	printf 'usage: %s SOURCE_C OVERRIDE_KERNEL_RELEASE [OUTPUT_KO]\n' "$0" >&2
	exit 2
fi

source_c=$1
kernel_release=${2:-$(uname -r)}
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=/lib/modules/$kernel_release/build
output_ko=${3:-/var/tmp/ov7251-illuminator-experiment-$kernel_release.ko}
expected_source_sha256=3588a52e0a3a4dfe23dd3425db95388d93af17c6f8eebe5b004ee8d1eea5aee9

test -f "$source_c"
test -f "$build_dir/Makefile"
test -f "$build_dir/.config"
test "$(sha256sum "$source_c" | awk '{print $1}')" = "$expected_source_sha256"

work_dir=$(mktemp -d /var/tmp/ov7251-illuminator-build.XXXXXX)
trap 'rm -rf "$work_dir"' EXIT

mkdir -p "$work_dir/drivers/media/i2c"
cp "$source_c" "$work_dir/drivers/media/i2c/ov7251.c"

for patch_file in \
	ov7251-stream-diagnostics.patch \
	ov7251-pll-mipi-readback.patch \
	ov7251-mipi-state-readback.patch \
	ov7251-illuminator-experiment.patch; do
	patch_path="$repo_root/patches/$patch_file"
	patch --dry-run --batch --forward --fuzz=0 -d "$work_dir" -p1 < "$patch_path"
	patch --batch --forward --fuzz=0 -d "$work_dir" -p1 < "$patch_path"
done

printf '%s\n' 'obj-m += ov7251.o' > "$work_dir/drivers/media/i2c/Makefile"

python3 "$repo_root/scripts/ir/check-ov7251-illuminator-experiment.py" \
	"$work_dir/drivers/media/i2c/ov7251.c"
python3 "$repo_root/scripts/ir/test-ov7251-illuminator-failure-path.py"

make -C "$build_dir" M="$work_dir/drivers/media/i2c" \
	EXTERNAL_BUILD=1 W=1 modules

install -D -m 0644 "$work_dir/drivers/media/i2c/ov7251.ko" "$output_ko"
sha256sum "$output_ko"
modinfo "$output_ko" | grep -E '^(filename|name|depends|vermagic|parm):'
