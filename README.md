# Surface Pro 7 IPU4P Camera Driver

This project provides out-of-tree Linux driver modules and Surface Pro 7
compatibility fixes for the Intel IPU4P image signal processor (PCI ID
`8086:8a19`). It carries an out-of-tree port based on
[ruslanbay/ipu4-next](https://github.com/ruslanbay/ipu4-next), adapted for the
Surface Pro 7 camera hardware.

The code has been hardware-validated on one Surface Pro 7 running Fedora 43
with the linux-surface kernel `6.19.8-3.surface.fc43.x86_64`. V4L2 raw capture
has succeeded from the front OV5693 and rear OV8865 cameras. Fedora's existing
libcamera Simple pipeline with SoftISP has also produced processed captures
from both cameras. The GNOME Snapshot and portal startup path has not been
qualified.

## Support and project scope

The release workflow is configured to build modules for this exact kernel release.
These binaries are kernel-specific; install only the asset whose kernel
release matches `uname -r`. Other kernels require a matching prepared kernel
build tree and may need kernel-tree integration for the OV5693 sensor source.

This repository contains the IPU4P parent, ISYS and PSYS drivers, CSS libraries,
bridge support, Surface Pro 7 compatibility fixes, and build/install/test
scripts. It is an overlay for a Linux kernel tree, not a complete kernel or a
standalone camera application.

The rear OV8865 sensor driver is an external requirement and is not included
here. The IR OV7251 camera's I2C probe fails on the validated device. The CPD
firmware `ipu4p_cpd.bin` is required at runtime but is not redistributed by
this project. Applications and desktop camera services are supplied by the
distribution.

## Install from GitHub Releases

### Check the kernel

```sh
uname -r
```

The release workflow currently targets:

```text
6.19.8-3.surface.fc43.x86_64
```

Do not install that asset if `uname -r` reports a different release. A module
must be built for the exact kernel that will load it.

### Download and install

Open the [GitHub Releases page](https://github.com/georgemihaila/sp7-ipu4-camera/releases)
and download the `.tar.gz` asset whose filename includes the running kernel
release. When a GitHub Release is published, the
[release workflow](.github/workflows/driver-release.yml) builds and attaches
the matching bundle.

The bundle does not contain `ipu4p_cpd.bin`. Obtain that Microsoft-signed
firmware from an authorized source, then extract and verify the archive:

```sh
mkdir -p sp7-ipu4-camera
tar -xzf /path/to/sp7-ipu4-camera-VERSION-6.19.8-3.surface.fc43.x86_64.tar.gz \
  -C sp7-ipu4-camera
cd sp7-ipu4-camera
sha256sum -c SHA256SUMS
```

Install the modules and firmware by giving the installer the firmware path:

```sh
FIRMWARE=/absolute/path/to/ipu4p_cpd.bin sudo -E ./scripts/install-modules.sh
```

The installer checks each module's embedded kernel version. It installs the
modules under `/lib/modules/<kernel-release>/updates/extra/`, installs the
firmware at `/lib/firmware/ipu4p_cpd.bin` if it is not already present, and
runs `depmod`.
It refuses to overwrite untracked or modified module files and conflicting
firmware. It does not load modules, change the bootloader, manage services, or
rebuild the initramfs.

Reboot after installation. The PCI modalias and module dependencies load the
driver during normal device discovery; no manual module loader or systemd
service is needed. If Secure Boot requires signed modules, sign the modules
with a key trusted by the system before loading them.

### Uninstall

From the extracted bundle, run:

```sh
sudo ./scripts/uninstall-modules.sh
```

The uninstaller removes only unchanged modules recorded by the install
manifest and updates the module dependency index when it removes modules. It
does not unload modules already running in memory, so reboot to complete
removal. Firmware is left in place because another driver may use it; remove
it separately only after confirming it is not shared.

## Build from source

The `linux-6.19.8/` directory is an overlay, not a complete kernel source
tree. Build against the prepared source/build tree for the exact target
kernel:

```sh
KDIR="/lib/modules/$(uname -r)/build" \
  ./scripts/build-modules.sh -j"$(nproc)"
```

The build script verifies the kernel release and the generated modules'
`vermagic`. It builds the IPU4P modules; it does not build the OV5693 sensor
source. Integrating that sensor source into a different kernel tree is
described in [docs/external-kernel-integration.md](docs/external-kernel-integration.md).
After building, install with the firmware command above and reboot.

## Validation

Run the hardware-independent checks with:

```sh
./tests/camera-suite.sh
```

For bounded capture checks on a running Surface Pro 7 with the driver already
loaded, use:

```sh
sudo ./tests/camera-suite.sh --live
```

Live checks require the camera tools, firmware, permissions, and a usable
media graph. They do not install, load, unload, or reload modules. A quick
manual capture is also available with `sudo ./test-capture.sh front` or
`sudo ./test-capture.sh rear`.

## Credits and licensing

The driver code in `linux-6.19.8/drivers/media/pci/intel/` is GPL-2.0 (Linux
kernel / Intel, with changes from ruslanbay/ipu4-next and this repo). Scripts
in the repo root are GPL-2.0 as well.

- https://github.com/ruslanbay/ipu4-next — the port this builds on
- https://github.com/linux-surface/linux-surface — SP7 kernel; camera discussion in issue #1353
