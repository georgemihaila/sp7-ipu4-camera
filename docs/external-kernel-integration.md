# External kernel integration

`linux-6.19.8/` is an overlay-only fragment. It is not a complete Linux
source tree: the repository intentionally does not contain the top-level
kernel files, generated configuration, or unrelated drivers. Do not point
`make -C` at this directory as if it were a kernel tree.

## IPU module build

Install a prepared kernel source/build tree whose configuration matches the
kernel that will load the modules. The minimum standalone build is:

```sh
KDIR=/lib/modules/$(uname -r)/build ./scripts/build-modules.sh
```

`KDIR` may instead name a checked-out kernel source tree with a prepared
`.config`. The script passes the overlay's Intel directory as both `M` and
`srcpath`; the latter is required by the fragment's IPU4 CSS makefiles.
`CONFIG_VIDEO_INTEL_IPU=m`, `CONFIG_VIDEO_INTEL_IPU4P=y`, and
`CONFIG_VIDEO_INTEL_IPU_FW_LIB=y` are supplied for this external build.

This builds the IPU parent, ISYS, PSYS, and CSS-library modules. It does not
build `ov5693.c`, because that file is in the kernel's `drivers/media/i2c/`
namespace and the fragment has no replacement for the kernel's i2c Makefile.

## Required kernel-tree integration for a complete camera build

For an integrated build, apply the overlay to the matching kernel source tree
and add the sensor to that tree's existing media-i2c build/Kconfig lists. The
exact paths are:

```text
KERNEL_SRC=/path/to/linux-6.19.8
cp -a linux-6.19.8/drivers/media/pci/intel/. \
  "$KERNEL_SRC/drivers/media/pci/intel/"
cp linux-6.19.8/drivers/media/i2c/ov5693.c \
  "$KERNEL_SRC/drivers/media/i2c/ov5693.c"
```

Then add `ov5693.o` to the kernel tree's `drivers/media/i2c/Makefile` under
the same configuration symbol used by that tree's other V4L2 sensor drivers,
and add/enable its corresponding Kconfig entry. The IPU fragment's
`drivers/media/pci/intel/Makefile` and `Kconfig` must be merged with, not
blindly appended beside, the matching upstream files; retain the existing
`ipu6`/`ivsc` entries from that kernel tree. This integration step is
kernel-version-specific and is intentionally not represented as a fake distro
package or an unapplyable universal patch.

Before installing, verify the resulting `.config` enables the required media,
I2C, ACPI, PCI, DMA-BUF, runtime-PM, and V4L2/media-controller dependencies.
