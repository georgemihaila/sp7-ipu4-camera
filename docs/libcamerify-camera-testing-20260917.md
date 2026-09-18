# libcamerify camera testing — 2026-09-17

## Scope and outcome

This was a local, no-reboot, no-call test on the Fedora Surface Pro 7 host in
`/home/george/repos/sp7-ipu4-camera`, branch `optimize`, kernel
`6.19.8-3.surface.fc43.x86_64`. The working tree was clean before testing.

The result is **retain the bridge**. Host `libcamerify` can stream the rear
sensor, but its current device-to-camera mapping cannot select the front
sensor: both physical cameras report the same IPU4P `SystemDevices` list and
the compatibility layer takes the first matching camera. Neither installed
Flatpak contains a compatible libcamera compatibility layer, and host-side
preloading does not cross the Flatpak sandbox boundary. Therefore this is not
a practical replacement for the installed Zoom and Discord clients.

No Zoom or Discord call was joined, and no video was transmitted.

## Feasibility before capture

| Item | Observed result |
|---|---|
| Host | Fedora x86_64; kernel `6.19.8-3.surface.fc43.x86_64`; libcamera RPM `0.7.1-1.fc44`; `libcamera-ipa`, `libcamera-tools`, and `pipewire-plugin-libcamera` also `0.7.1-1.fc44` |
| Zoom | Flatpak `us.zoom.Zoom` `7.1.5.4332`, x86_64, Flathub, runtime `org.freedesktop.Platform/x86_64/25.08` |
| Discord | Flatpak `com.discordapp.Discord` `1.0.158`, x86_64, Flathub, runtime `org.freedesktop.Platform/x86_64/25.08` |
| Flatpak permissions | Both reported `devices=all`; no user or system Flatpak override was present. Discord also has `xdg-run/pipewire-0`. |
| Compatibility package | Fedora offered `libcamera-v4l2-0.7.1-1.fc44`; it was installed temporarily for the host test and removed during restoration. |
| Flatpak contents | Neither sandbox contained `libcamerify`, `/usr/libexec/libcamera/v4l2-compat.so`, or libcamera shared libraries. |
| Physical cameras | Direct libcamera enumerated `ov8865` as rear and `ov5693` as front through the Simple pipeline with `ipa_soft_simple.so` / SoftISP. |

The upstream design matches the observed behavior:

* The [V4L2 compatibility source](https://github.com/libcamera-org/libcamera/blob/master/src/v4l2/v4l2_compat.cpp) injects wrappers for calls such as `open`, `ioctl`, `mmap`, and `close`.
* The [build definition](https://github.com/libcamera-org/libcamera/blob/master/src/v4l2/meson.build#L334-L363) installs `v4l2-compat.so` and a `libcamerify` wrapper that sets `LD_PRELOAD`.
* The [camera mapper](https://github.com/libcamera-org/libcamera/blob/master/src/v4l2/v4l2_compat_manager.cpp#L840-L908) matches a device number against each camera's `SystemDevices` property and explicitly uses the first match as a best-effort mapping. It also notes that exposing every camera through a single stable video node remains a TODO.
* The [Simple pipeline](https://github.com/libcamera-org/libcamera/blob/master/src/libcamera/pipeline/simple/simple.cpp#L2285-L2502) relies on generic Media Controller/V4L2 subdevice support and may share hardware resources between cameras.

The two Flatpak route probes produced these relevant results:

```text
ERROR: ld.so: object '/usr/libexec/libcamera/v4l2-compat.so' from LD_PRELOAD cannot be preloaded (cannot open shared object file): ignored.
id=us.zoom.Zoom
compat_exists=1
libcamera_exists=1

id=us.zoom.Zoom                 # host-side libcamerify flatpak run
preload=
compat_exists=1
```

Discord produced the same result. The first command used a temporary
per-launch `--env=LD_PRELOAD=...`; the second tested
`libcamerify flatpak run ...`. The first could not find the library inside the
sandbox. The second did not pass `LD_PRELOAD` into the sandbox. No host
library was bind-mounted or preloaded into a different runtime.

## Preserving the working setup

Before changing runtime state, the effective WirePlumber policy, loopback
options, user service, bridge binary, service states, Flatpak overrides, and
device ownership were backed up under a temporary directory outside the
repository.

The reversible direct-test transition was:

```sh
systemctl --user stop sp7-camera-bridge.service
systemctl --user mask --runtime sp7-camera-bridge.service
```

WirePlumber remained active with its original policy. In particular, its
physical libcamera monitor stayed disabled, its raw-IPU4 hide rule stayed in
place, and no physical libcamera device was opened by WirePlumber. The OBS
loopback `/dev/video55` and the named bridge loopbacks were excluded from
native-camera results. No sensor controls, kernel modules, FlatPak overrides,
permissions, or application settings were changed.

## Direct libcamera baseline

With the bridge stopped and masked, `cam -l` reported:

```text
1: Internal back camera (\_SB_.PCI0.I2C3.CAMR)
2: Internal front camera (\_SB_.PCI0.I2C2.CAMF)
```

The exact direct capture commands were:

```sh
cam --camera 2 --info
cam --camera 1 --info
cam --camera 2 --capture=3 --file=/tmp/front-#.ppm
cam --camera 1 --capture=3 --file=/tmp/rear-#.ppm
```

Results:

| Sensor | Direct stream | Evidence |
|---|---:|---|
| Front `ov5693` | `2588x1944 ABGR8888`, about 28.7 fps | Three PPM frames, 15,093,233 bytes each, changing SHA-256 values; ImageMagick mean values `63517.2`, `62552.3`, and `62976`; visible non-black scene content. |
| Rear `ov8865` | `3260x2448 ABGR8888`, about 15.0 fps | Three PPM frames, 23,941,457 bytes each, changing SHA-256 values; ImageMagick mean values `4870.1`, `6094.41`, and `15243.7`; visible non-black scene content. |

The direct logs show `SoftwareIsp` input formats and nonzero `bytesused`
(`20404224` front and `31961088` rear). Expected non-fatal messages were
logged for unsupported IPU4 TPG nodes and missing sensor YAML files, which
fell back to the uncalibrated Simple IPA configuration.

## Independent V4L2 compatibility test

The small installed V4L2 client was `v4l2-ctl`. The compatibility package was
temporarily installed with:

```sh
sudo dnf install -y libcamera-v4l2
```

The wrapper was verified with:

```sh
libcamerify v4l2-ctl --list-devices
libcamerify v4l2-ctl --device=/dev/video0 --list-formats-ext
```

The wrapped enumeration exposed `/dev/video0` through `/dev/video51`,
`/dev/video53`, and `/dev/video54` as:

```text
Card type: _SB_.PCI0.I2C3.CAMR
Bus info:  libcamera:0
```

That is the rear `ov8865`. `/dev/video52` remained the raw `ipu4p` media
device. No front `CAMF` compatibility device was exposed. `cam --camera 1
--list-properties` and `cam --camera 2 --list-properties` showed the same
`SystemDevices` values for rear and front (`20739` through `20793` with the
same gaps), so the first-match behavior documented upstream explains why all
shared nodes resolve to rear.

Format negotiation and one-frame streaming were exercised at a supported
resolution:

```sh
libcamerify v4l2-ctl --device=/dev/video0 \
  --set-fmt-video=width=640,height=480,pixelformat=AB24 \
  --stream-mmap=4 --stream-count=1 --stream-to=/tmp/rear-640x480-ab24.raw
```

The output was 1,228,800 bytes. Converted image statistics were nonzero
(`mean=3535.03`, `min=0`, `max=51914`) and the visible frame matched the rear
scene. The compatibility process log configured
`640x480-ABGR8888/sRGB` and named both libcamera cameras during discovery.

The long-running test used the same command without `--stream-count`, ran for
more than 30 seconds by wall clock, and streamed to `/dev/null`. During the
run:

* `/proc/$pid/maps` contained `/usr/libexec/libcamera/v4l2-compat.so` and the
  host libcamera libraries.
* `/dev/video42` was owned by the wrapped `v4l2-ctl` process; no loopback was
  counted as native capture.
* The compatibility path reported about 92.6 fps at 640x480 and continued
  producing frames for the full duration.
* The first `SIGINT` did not make this `v4l2-ctl` process exit, so the test
  process was terminated with `SIGTERM` after the required duration. This is
  recorded as a clean-shutdown limitation, not hidden as a pass.

Afterward, three sequential reopen tests all returned status 0:

```sh
for attempt in 1 2 3; do
  libcamerify v4l2-ctl --device=/dev/video0 \
    --set-fmt-video=width=640,height=480,pixelformat=AB24 \
    --stream-mmap=4 --stream-count=3 --stream-to=/dev/null
done
```

The compatibility client therefore passed rear enumeration, format
negotiation, image delivery, long streaming, and reopening. It failed the
front-camera and switchability requirements, and its tested shutdown path
required termination.

## Application deployment routes and application trials

### Route 1: existing Flatpak runtime

**Unsupported for both installed clients.** The runtime has no compatible
libcamera stack or V4L2 compatibility library. A host-side wrapper cannot
solve that because Flatpak strips the host preload before entering the
sandbox. No broad permission override was attempted.

### Route 2: temporary development Flatpak build

**Not practical for this installation.** Zoom is a proprietary bundled
client, and Discord is a bundled Electron client. No application source or
Flatpak manifest was available in this checkout. A meaningful temporary build
would need to add matching libcamera libraries, the V4L2 compatibility layer,
IPA modules, configuration files, and their dependent runtime libraries to a
repacked application. That was not attempted because it would no longer be a
test of the installed application bundle.

### Route 3: native application package

**Not tested.** The plan requires asking before installing an additional
application package. No native Zoom or Discord package was installed, so no
native result is attributed to either installed Flatpak.

| Application / packaging | Wrapper loaded | Front video | Rear video | Switching / reopening | Limitations |
|---|---|---|---|---|---|
| Small V4L2 test client (`v4l2-ctl`) | Yes | Tested but unavailable through the compatibility mapping | Tested and passed | Reopen passed; long-run interrupt did not | All shared nodes mapped to rear; `FrameDurationLimits` was unsupported; no front selection. |
| Zoom Flatpak `7.1.5.4332` | No; sandbox library absent | Not tested | Not tested | Not tested | Existing runtime cannot load host `libcamerify`; no GUI surface was available for local video settings. |
| Discord Flatpak `1.0.158` | No; sandbox library absent | Not tested | Not tested | Not tested | Existing runtime cannot load host `libcamerify`; no GUI surface was available for local video settings. |
| Native package | Not tested | Not tested | Not tested | Not tested | No additional application package was installed. |

The computer-control inventory contained no native app surface, only the Codex
in-app browser. Thus Zoom and Discord enumeration, preview, lens-cover
identity, switching, reopen, relaunch, and application-level restoration were
not observed and are intentionally marked **not tested**, not inferred from
the V4L2 result.

## Restoration and bridge check

The temporary mask was removed and the bridge was restarted:

```sh
systemctl --user unmask sp7-camera-bridge.service
systemctl --user start sp7-camera-bridge.service
```

The host-only test package was removed:

```sh
sudo dnf remove -y libcamera-v4l2
```

Restoration checks passed:

* `sp7-camera-bridge.service` is active and enabled.
* WirePlumber and PipeWire are active.
* `/dev/video55` remains `OBS Virtual Camera`.
* `/dev/video60` remains `Surface Camera (front)` and `/dev/video61` remains
  `Surface Camera (back)`.
* The effective WirePlumber policy, loopback options, user service, and
  installed bridge binary have the same SHA-256 values as before testing.
* `wpctl status` exposes only the three expected video devices: OBS virtual,
  Surface Camera (front), and Surface Camera (back).
* No `libcamerify`, `v4l2-ctl`, or SoftISP helper from the direct test remains.

As a bridge-only warm-up check, each named endpoint was consumed for 120
GStreamer buffers after restart:

```sh
gst-launch-1.0 -q v4l2src device=/dev/video60 io-mode=mmap num-buffers=120 \
  ! 'video/x-raw,format=YUY2,width=1280,height=720,framerate=30/1' \
  ! jpegenc ! multifilesink location=/tmp/bridge-front-%03d.jpg

gst-launch-1.0 -q v4l2src device=/dev/video61 io-mode=mmap num-buffers=120 \
  ! 'video/x-raw,format=YUY2,width=1280,height=720,framerate=30/1' \
  ! jpegenc ! multifilesink location=/tmp/bridge-rear-%03d.jpg
```

The first frames were the documented startup filler (`mean=4112`). The final
front frame had `mean=44293`, `min=0`, `max=65535`; the final rear frame had
`mean=18659.7`, `min=6425`, `max=62194`. Both final frames contained visible,
non-black scene content and differed by sensor. The short initial loopback
probe was deliberately not counted because it ended during startup warm-up.

## Recommendation

Retain the bridge. `libcamerify` is a useful host-side experiment and proves a
working rear-camera V4L2 path, but it cannot select the front camera on this
IPU4P graph and is unavailable inside the installed Zoom and Discord
Flatpaks. Revisit only after a runtime-integrated libcamera deployment can
provide stable per-sensor device mapping and the application GUI trials can
be performed. Do not create a permanent wrapper or remove the bridge based on
this result.
