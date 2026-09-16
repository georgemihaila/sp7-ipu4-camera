# Named Surface Cameras for V4L2 applications

The kernel exposes the IPU4P's raw capture nodes as a single `ipu4p` V4L2
device. Fedora's libcamera monitor separately discovers the front and rear
sensors, but many applications enumerate V4L2 devices directly. This bridge
supplies two stable V4L2 names backed by the existing libcamera cameras:

| Camera name | Device | Sensor |
|---|---|---|
| Surface Camera (front) | `/dev/video60` | OV5693 |
| Surface Camera (back) | `/dev/video61` | OV8865 |

The IPU4P shares a backend capture route between the sensors. The user service
keeps both virtual devices capturable with an idle black signal, watches for an
application opening one, and starts only that sensor's GStreamer pipeline. It
releases the route when the virtual device is no longer in use. Leave
`exclusive_caps=0` for these two devices so applications can enumerate them
before the real camera producer starts. The RPM Fusion OBS virtual camera stays on
`/dev/video55` with its existing exclusive-caps behavior.

## Install

On Fedora, the top-level driver installer installs these dependencies and
enables the bridge automatically. For a release bundle or a bridge-only
installation, install the GStreamer libcamera source and RPM Fusion loopback
module, then build the module for the running kernel:

```sh
sudo dnf install libcamera-gstreamer akmod-v4l2loopback v4l2loopback
sudo akmods --force --kernels "$(uname -r)"
```

From this repository, run:

```sh
sudo ./scripts/setup-camera-bridge.sh
```

The setup installs the module labels, loads the loopback nodes, and enables a
per-user systemd service. It overrides RPM Fusion's same-named modprobe file
with an `/etc` configuration that keeps the OBS virtual camera and adds the
two SP7 devices. It also installs the WirePlumber rule that hides the raw
`ipu4p` nodes from normal application enumeration and lets the bridge release
the shared backend when needed.

Restart applications that were already open so they rescan the new V4L2
devices. Choose **Surface Camera (front)** or **Surface Camera (back)**. The
bridge streams only the selected camera. While it is active, the bridge
temporarily stops WirePlumber so the direct libcamera pipeline can own the
shared IPU4P backend; WirePlumber is restarted when the client closes. The two
physical sensors cannot be captured simultaneously through this backend.

The bridge reads each loopback endpoint’s current V4L2 format before starting a producer. It emits packed YUYV through `videoconvert` when the endpoint is set to YUYV, and encodes the same 1280x720 stream with `jpegenc` when an application has selected MJPG/JPEG. This keeps consumers such as Zoom from leaving the producer with a `not-negotiated` pipeline.

To remove the bridge and restore RPM Fusion's default OBS module options:

```sh
sudo ./scripts/remove-camera-bridge.sh
```

This leaves the GStreamer and RPM Fusion packages installed.
