# Named Surface Cameras for V4L2 applications

The kernel exposes the IPU4P's raw capture nodes as a single `ipu4p` V4L2
device. Fedora's libcamera monitor separately discovers the front and rear
sensors, but many applications enumerate V4L2 devices directly. This bridge
supplies two stable V4L2 names backed by the existing libcamera cameras:

| Camera name | Device | Sensor |
|---|---|---|
| Surface Camera (front) | `/dev/video60` | OV5693 |
| Surface Camera (back) | `/dev/video61` | OV8865 |
| Surface Camera (IR) | `/dev/video62` | OV7251 packed RAW10 source-6 producer |

The IPU4P shares a backend capture route between the RGB sensors. The user service
keeps both virtual devices capturable with an idle black signal, watches for an
application opening one, and starts only that sensor's GStreamer pipeline. It
releases the route when the virtual device is no longer in use. The OBS
The front/rear endpoints use `exclusive_caps=1`. They report `OUTPUT` until
the bridge opens each producer and then report
`CAPTURE` for camera applications. The bridge unit starts after PipeWire and
before WirePlumber, and its bounded `ExecStartPost` readiness barrier checks
both `Device Caps` blocks for `Video Capture`; a timeout fails the service
instead of allowing WirePlumber to cache the initial output-only state. The
check uses `v4l2-ctl --all` because `--list-formats-ext` can print a capture
format while the device capabilities still advertise `Video Output` only.

The IR loopback is additive and intentionally outside the front/rear bridge
controller. Its `exclusive_caps=1` node stays output-only until the standalone
`sp7-camera-ir` producer is started; the bridge service does not start it,
watch it, or switch RGB requests to it. The producer discovers the enabled
OV7251-to-source-6 media graph, validates the negotiated packed-RAW10 buffer,
decodes it to grayscale YUYV with neutral chroma, and writes `/dev/video62`.
Concurrency with RGB capture remains unqualified and must not be inferred from
enumeration.

## Install

On Fedora, the top-level installer defaults to a full source installation:

```sh
sudo ./install.sh --full
```

To install only the kernel driver and firmware, without bridge packages or
desktop configuration, use:

```sh
sudo ./install.sh --driver-only
```

For a prebuilt release archive, `scripts/install-modules.sh` is the IPU4P,
OV7251 IR-driver, and firmware installer; it does not install compiler or
development packages. The installed OV7251 illuminator control remains
disabled by default.
For a bridge-only installation on an already prepared host, install the
runtime dependencies and build the loopback module first:

```sh
sudo dnf install libcamera-gstreamer gstreamer1-plugins-good akmod-v4l2loopback v4l2loopback
sudo akmods --force --kernels "$(uname -r)"
```

From this repository, run:

```sh
sudo ./scripts/setup-camera-bridge.sh
```

The setup checks every GStreamer element used by the filler and camera
pipelines and reports the missing element and package group if a plugin is not
available. Firmware extraction tooling is installed by `install.sh` only when
no caller-supplied or standard installed CPD firmware is available.

The source installer builds the C bridge in `cbridge/` and setup installs it as
`/usr/local/libexec/sp7-camera-bridge`. A prebuilt release archive carries the
same binary, so bridge setup does not require a compiler or development
packages. The C executable is the only supported camera bridge.

The setup installs the module labels, loads the loopback nodes, and enables a
per-user systemd service. It overrides RPM Fusion's same-named modprobe file
with an `/etc` configuration that keeps the OBS virtual camera and adds the
two SP7 RGB devices plus the opt-in IR loopback. It also installs a WirePlumber policy that hides the raw
`ipu4p` nodes and disables the physical libcamera monitor. This leaves
WirePlumber serving the named loopback devices without opening the shared
backend itself. The bridge owns the physical sensor only while an application
consumes its corresponding named endpoint.

## Application limitation

This is the fallback bridge mode, not the native PipeWire/libcamera camera
mode. Because the installed policy disables the physical libcamera monitor,
GNOME Snapshot and other clients that require native PipeWire/libcamera
sources can report that no camera was found even while V4L2 clients use the
named bridge devices successfully. That result is expected under the default
policy and does not show that the kernel cameras are absent.

The native profile in
[`docs/native-pipewire.md`](native-pipewire.md) is an explicit opt-in
qualification path. It must not be enabled as an enumeration-only workaround:
the current native PipeWire result is known-bad (black frames), although
direct `cam` captures remain valid.

Restart applications that were already open so they rescan the new V4L2
devices. Choose **Surface Camera (front)** or **Surface Camera (back)**. The
bridge streams only the selected camera. It opens the selected physical sensor
directly while WirePlumber remains active for the application's PipeWire
loopback target. The two physical sensors cannot be captured simultaneously
through this backend.

The bridge reads each loopback endpoint’s current V4L2 `VIDEO_CAPTURE` format
before starting a producer and falls back to `VIDEO_OUTPUT` while an
exclusive-caps endpoint is still in its unopened producer state. It emits packed
YUYV through `videoconvert` when the endpoint is set to YUYV, and encodes the
same 1280x720 stream with `jpegenc` when an application has selected MJPG/JPEG.
This keeps consumers such as Zoom and Snapshot on one fixed capture format
instead of an unfixed PipeWire caps set.

To remove the bridge and restore RPM Fusion's default OBS module options:

```sh
sudo ./scripts/remove-camera-bridge.sh
```

This leaves the GStreamer and RPM Fusion packages installed.
