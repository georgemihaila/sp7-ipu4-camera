# Surface Pro 7 IPU4P Camera Driver — Proof of Concept

This project provides out-of-tree Linux driver modules and Surface Pro 7
compatibility fixes for the Intel IPU4P image signal processor (PCI ID
`8086:8a19`). It carries an out-of-tree port based on
[ruslanbay/ipu4-next](https://github.com/ruslanbay/ipu4-next), adapted for the
Surface Pro 7 camera hardware. It includes a [back camera](#back-camera), a
[front camera](#front-camera), an [IR camera](#ir-camera), an [IR
illuminator](#ir-illuminator), and [manual focus](#manual-focus), not
autofocus.

The code has been hardware-validated on one Surface Pro 7 running Fedora 43
with the linux-surface kernel `6.19.8-3.surface.fc43.x86_64`. V4L2 raw capture
has succeeded from the front OV5693 and rear OV8865 cameras. Fedora's existing
libcamera Simple pipeline with SoftISP has also produced processed captures
from both cameras. The default full installation uses the named V4L2 bridge,
intentionally disables WirePlumber's physical libcamera monitor, and therefore
does not expose a native camera to GNOME Snapshot; the app can report that no
camera was found while bridge-backed applications continue to work. The
opt-in native PipeWire path remains unqualified and is currently known to
produce black frames, so enabling it is not yet a fix for that symptom.

## Proof-of-concept status

This repository is a hardware-specific proof of concept and engineering
reference, not a production-ready Linux camera driver. It is not intended for
production deployment, broad hardware support, or use as a finished upstream
driver. The implementation is deliberately tied to one Surface Pro 7 and one
kernel build, and important production work—such as broader lifecycle,
power-management, error-recovery, portability, and application qualification—
remains incomplete.

The goal is to make the working experiments, register paths, media topology,
and validation evidence available to kernel and media developers who can use
them to design and implement a proper, maintainable driver. Treat every result
below as scoped evidence for this hardware and configuration, not as a claim
of production readiness.

## Support and project scope

The release workflow is configured to build the IPU4P stack and patched OV7251
IR camera module for this exact kernel release. The OV7251 illuminator control
is installed read-only; installation enables its STROBE/frame-PWM output and
bounded register diagnostics through a modprobe configuration fragment.
These binaries are kernel-specific; install only the asset whose kernel
release matches `uname -r`. Other kernels require a matching prepared kernel
build tree and may need kernel-tree integration for the OV5693 sensor source.

This repository contains the IPU4P parent, ISYS and PSYS drivers, CSS libraries,
the OV7251 IR sensor/illuminator module build, bridge support, Surface Pro 7
compatibility fixes, and build/install/test scripts. It is an overlay for a
Linux kernel tree, not a complete kernel or a standalone camera application.

The rear OV8865 sensor driver is an external requirement and is not included
here. The IR OV7251 path is provided separately as an opt-in standalone
producer, rather than being managed by the front/rear bridge. The CPD firmware
`ipu4p_cpd.bin` is required at runtime but is not included in this repository
or its release bundles. The source installer can obtain it from the official
Microsoft Surface Pro 7 driver package. Applications and desktop camera
services are supplied by the distribution.

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

The installer also builds and installs the C camera bridge, installs the
libcamera and v4l2loopback dependencies and,
when run from a logged-in desktop session, enables the named **Surface Camera
(front)** and **Surface Camera (back)** V4L2 endpoints for applications that
enumerate camera devices directly. Setup also reserves **Surface Camera (IR)**
on `/dev/video62`; its standalone producer is opt-in and is not started by the
front/rear bridge service.

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

The Phase 5 V4L2 compliance gate is separate and read-only:

```sh
V4L2_COMPLIANCE_DIR="$PWD/reports/v4l2-compliance" \
    ./tests/v4l2-compliance-validation.sh --live
```

It dynamically follows each `/sys/class/video4linux/video*` node to its
sysfs driver/module identity, records that node's capabilities, formats,
module metadata, and requested firmware, and runs `v4l2-compliance` only on
module-backed physical capture queues. Virtual, `v4l2loopback`, and
application-bridge identities are excluded from the discovered sysfs identity;
no video minor is assumed. If `v4l2-compliance`, `v4l2-ctl`, `timeout`, usable
hardware, or an eligible capture node is missing, the result is `SKIP`. A
non-zero compliance result for a node that was actually tested is `FAIL`.
This is ABI evidence only: it does not prove advancing/non-black frames,
power/lifetime correctness, PipeWire behavior, or application support. The
current validation host does not have `v4l2-compliance`, so no compliance
pass is claimed here.

For kernel readiness, use the exact prepared target `KDIR` and run a warning-
enabled module build, checkpatch on the actual patch series, and sparse at
the normal `C=1`/`C=2` levels:

```sh
make -C "$KDIR" M="$PWD/linux-6.19.8/drivers/media/pci/intel" W=1 modules
scripts/checkpatch.pl --strict <patch-series.patch
make -C "$KDIR" M="$PWD/linux-6.19.8/drivers/media/pci/intel" \
    W=1 C=1 CHECK=sparse CF='-D__CHECK_ENDIAN__' modules
make -C "$KDIR" M="$PWD/linux-6.19.8/drivers/media/pci/intel" \
    W=1 C=2 CHECK=sparse modules
```

Run the equivalent smatch configuration when supplied by the target kernel
tree. These checks depend on kernel-tree integration and are not implied by
the repository's shell/static suite or an out-of-tree module build alone.

The read-only native Phase 0 inventory can be run independently. It discovers
current media, video, and sub-device identities dynamically and records graph,
V4L2, module/firmware, libcamera, and PipeWire status; missing hardware or
tools are reported as skips:

```sh
NATIVE_INVENTORY_DIR="$PWD/reports/native-inventory" \
    ./tests/native-inventory.sh --live
```

This inventory is not a substitute for proving advancing non-black frames or
preview/capture in a named application. The C bridge and `v4l2loopback` remain
fallback/test tooling while native qualification is incomplete.

Application support remains unclaimed. The reproducible Phase 4 matrix in
[`docs/application-qualification.md`](docs/application-qualification.md) must
show moving preview, usable capture, lifecycle and front/rear-switch evidence,
package/sandbox provenance, portal results, and kernel/PipeWire logs for each
named application. Enumeration, direct-node access, or the C bridge does not
qualify Snapshot, Chromium/WebRTC, Zoom, Discord, or another application.

The native PipeWire phase is separately gated and currently unqualified. See
[`docs/native-pipewire.md`](docs/native-pipewire.md) for the opt-in WirePlumber
profile, activation/rollback, and the read-only `pipewiresrc` preview check.

## Camera features

### Back camera

The back camera uses the OV8865 sensor. Its raw stream travels through the
shared IPU4P CSI-2 and ISYS capture path; Fedora's libcamera Simple pipeline
with SoftISP can turn that raw stream into processed frames. For applications
that enumerate V4L2 devices, the C bridge publishes it as **Surface Camera
(back)** on `/dev/video61`. The bridge starts the rear pipeline when a client
opens that endpoint and publishes YUYV or MJPEG according to the requested
format. The shared backend selects one RGB sensor at a time.

See [`docs/surface-cameras.md`](docs/surface-cameras.md) for the bridge
lifecycle and endpoint details.

For the sensor-to-CSI-2-to-BE-SOC route, lane and format contract, raw Bayer
capture, and the bridge's control/data path, see
[`docs/back-camera.md`](docs/back-camera.md).

### Front camera

The front camera uses the OV5693 sensor and follows the same IPU4P CSI-2,
ISYS, and libcamera/SoftISP path as the back camera. The V4L2 bridge publishes
it as **Surface Camera (front)** on `/dev/video60`, starts it on demand, and
converts the selected stream to the format requested by the V4L2 application.

For OV5693 register controls, the Surface Pro 7 source-7/two-lane receiver
programming, raw capture commands, and the front bridge path, see
[`docs/front-camera.md`](docs/front-camera.md).

### IR camera

The IR camera uses the OV7251 monochrome sensor. Its standalone producer
discovers the live media graph, configures the 640x480 packed `Y10` source-6
route, validates each MMAP buffer, decodes the packed RAW10 samples to
grayscale YUYV, and writes the result to **Surface Camera (IR)** on
`/dev/video62`. This path is additive and opt-in: `sp7-camera-bridge.service`
manages only the front and back cameras, so starting or stopping the IR
producer does not change the RGB bridge configuration.

See [`docs/ov7251-ir-backend.md`](docs/ov7251-ir-backend.md) for the capture
backend and its qualification boundaries.

For the complete source-6 route, MMAP buffer contract, CSI-2 packet headers,
RAW10 unpacking, validation rules, and YUYV publication, see
[`docs/ir-camera.md`](docs/ir-camera.md).

### IR illuminator

The OV7251 illuminator is controlled through the sensor's STROBE/frame-PWM
registers rather than through a generic USB-camera LED control. The sensor
driver read-modify-writes and verifies the frame-PWM enable bit
(`0x3b96[7]`) and the STROBE output gate (`0x3005[3]`): it enables PWM before
opening the output gate, then clears the gate before PWM during cleanup. The
unrelated register bits are preserved, and cleanup is tied to the sensor's
power-off path so the output is not intentionally left enabled.

The driver parameters retain safe compiled-in defaults, while the installer
enables them at module load time through
`/etc/modprobe.d/99-sp7-ov7251.conf`. This does not claim optical illumination
or change the module's driver logic. The exact I2C byte sequences,
read-modify-write ordering, cleanup, diagnostic reads, and build boundary are
documented in
[`docs/ir-illuminator.md`](docs/ir-illuminator.md). The original bounded
experiment record remains in
[`docs/ov7251-illuminator-experiment.md`](docs/ov7251-illuminator-experiment.md).

### Manual focus

Manual focus applies to the rear OV8865 camera's DW9719 voice-coil lens
actuator. The IPU4P bridge follows the firmware-described `lens-focus`
relationship, instantiates the actuator on I2C, and exposes the standard V4L2
`focus_absolute` control. Setting that control moves the lens to a chosen
absolute position; there is no continuous autofocus or automatic focus
algorithm in this project. Applications or a user-space focus tool must
choose and set the position.

For the firmware-to-fwnode-to-I2C-client relationship, V4L2 control callback,
DW9719 register bytes, power sequencing, and binding checks, see
[`docs/manual-focus.md`](docs/manual-focus.md).

## Named Surface Cameras

For the fallback named endpoints used while native qualification is incomplete,
see
[`docs/surface-cameras.md`](docs/surface-cameras.md) for the named front and
rear camera bridge. The driver install flow enables it automatically.

The source installer supports `sudo ./install.sh --driver-only` when only the
IPU4P/OV7251 drivers and firmware are wanted. The default `--full` mode also installs
and configures the named-camera bridge. A prebuilt release archive uses
`scripts/install-modules.sh` directly and does not install compiler or
development packages.

## Credits and licensing

The driver code in `linux-6.19.8/drivers/media/pci/intel/` is GPL-2.0 (Linux
kernel / Intel, with changes from ruslanbay/ipu4-next and this repo). Scripts
in the repo root are GPL-2.0 as well.

- https://github.com/ruslanbay/ipu4-next — the port this builds on
- https://github.com/linux-surface/linux-surface — SP7 kernel; [camera discussion #1353](https://github.com/linux-surface/linux-surface/discussions/1353)
