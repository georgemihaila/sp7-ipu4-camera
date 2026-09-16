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
firmware `ipu4p_cpd.bin` is required at runtime but is not included in this
repository or its release bundles. The source installer can obtain it from
the official Microsoft Surface Pro 7 driver package. Applications and desktop
camera services are supplied by the distribution.

The rear camera's DW9719 focus actuator is included as a module. Linux 6.19
dropped the I2C ID table required for its ACPI-created device, so this project
bundles a corrected module that exposes the V4L2 absolute focus-position
control. This makes lens movement available; automatic focus still requires a
userspace autofocus algorithm. The repository includes a manual one-shot
contrast sweep for the rear camera; desktop and libcamera autofocus remain
outside the supported application path.

## Install from a clone

On Fedora Linux x86_64 running a linux-surface kernel, clone the repository
and run the installer from the checkout:

```sh
git clone https://github.com/georgemihaila/sp7-ipu4-camera.git
cd sp7-ipu4-camera
sudo ./install.sh
```

The script installs the Fedora build tools and, if needed, configures the
linux-surface package repository to install a matching prepared kernel build
tree. It then builds the modules for the running kernel and installs them.
The project has been hardware-validated with the linux-surface kernel
`6.19.8-3.surface.fc43.x86_64` on Fedora 43; other kernel releases need a
matching prepared build tree and may need additional kernel integration.

The required camera firmware is not bundled with this repository. If
`/lib/firmware/ipu4p_cpd.bin` is absent, the script downloads Microsoft's
[official Surface Pro 7 driver package](https://www.microsoft.com/en-us/download/details.aspx?id=100419)
(Windows 11 package version `25.090.3489.0`, about 678 MB), unpacks it
temporarily to extract the firmware, and removes the temporary files when it
exits. Allow additional temporary disk space for extraction. To use a firmware
file already downloaded from Microsoft, pass its path explicitly:

```sh
sudo FIRMWARE=/path/to/cpd_component_signed.bin ./install.sh
```

Reboot after installation. Secure Boot may require signing the modules with a
key trusted by the system.

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

## Install from GitHub Packages

The [GitHub Packages container image](https://github.com/georgemihaila/sp7-ipu4-camera/pkgs/container/sp7-ipu4-camera)
provides the same bundle in addition to the direct Release download. The image
stores the archive at `/bundle.tar.gz`; on Fedora, extract it with Podman:

```sh
podman run --rm ghcr.io/georgemihaila/sp7-ipu4-camera:main \
  cat /bundle.tar.gz > sp7-ipu4-camera.tar.gz
```

Use `:v0.1.0` for the first versioned Release or `:latest` for the newest
versioned Release. The `:main` tag tracks the latest successful build from the
main branch. Continue with the checksum and installation steps above.

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

For a one-shot contrast-based focus sweep on the rear camera, run:

```sh
sudo ./autofocus-rear.sh
```

The script moves the DW9719 through a coarse-to-fine range, captures temporary
BE SOC raw frames, selects the sharpest tested position, and removes the
temporary captures. Keep a detailed, well-lit target still during the sweep.
It requires the DW9719 `focus_absolute` control, `media-ctl`, `v4l2-ctl`,
Python 3, and NumPy. `MIN_POS`, `MAX_POS`, `STEP`, and `LEVELS` can tune the
scan; run `./autofocus-rear.sh --help` for defaults. This is a one-shot
contrast sweep, not continuous autofocus or GUI/libcamera AF. When run via
sudo from a desktop session, it pauses the invoking user's active WirePlumber
service for the capture and restarts it on exit; it skips this when that
user's systemd bus or service is unavailable.

## Credits and licensing

The driver code in `linux-6.19.8/drivers/media/pci/intel/` is GPL-2.0 (Linux
kernel / Intel, with changes from ruslanbay/ipu4-next and this repo). Scripts
in the repo root are GPL-2.0 as well.

- https://github.com/ruslanbay/ipu4-next — the port this builds on
- https://github.com/linux-surface/linux-surface — SP7 kernel; camera discussion in issue #1353
