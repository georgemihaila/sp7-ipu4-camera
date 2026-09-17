# Native Linux webcam driver and camera-stack specification

**Project:** `sp7-ipu4-camera`

**Target hardware:** Microsoft Surface Pro 7, Intel IPU4P (`8086:8a19`), OV5693 front sensor, OV8865 rear sensor, and the rear DW9719 actuator where present

**Status:** design specification and implementation plan

**Date:** 2026-09-17
**Primary constraint:** no application-compatibility bridge (`v4l2loopback`,
the project C bridge, synthetic filler nodes, or an `LD_PRELOAD` V4L2 shim)

## 1. Executive decision

The product target is a native Linux camera stack with three cooperating
layers:

```text
camera sensor / actuator
        |
        | V4L2 sub-device + media-controller graph
        v
IPU4P CSI receiver, ISYS, PSYS/ISP, firmware
        |
        +--> native raw/debug V4L2 nodes
        |
        +--> native processed capture stream, if the IPU4P kernel path can
             expose an application-friendly format
                         |
                         v
                  libcamera pipeline handler + IPA
                         |
                         v
                  PipeWire libcamera source / portal
                         |
                         v
                  Snapshot, browsers, desktop applications
```

The kernel driver must expose a truthful V4L2/media graph and reliable capture
queues. It must not create application-specific camera names, copy frames into
loopback devices, or implement desktop fan-out. For this IPU4P camera, the
libcamera pipeline is not optional if the usable image requires coordinated
sensor, receiver, ISP, firmware, and 3A configuration: libcamera explicitly
exists to manage complex camera pipelines above the kernel V4L2 and Media
Controller APIs ([libcamera introduction](https://docs.libcamera.org/master/)).

The application requirement therefore has two native paths:

1. Direct V4L2 applications use a real driver-owned `/dev/video*` capture node.
2. Complex-camera applications use the native libcamera pipeline, exposed by
   PipeWire/WirePlumber and, for sandboxed applications, the XDG Camera portal.

This is not the same as a synthetic compatibility bridge. PipeWire and the
portal are desktop transport and permission layers; they do not manufacture a
second V4L2 camera node. The project must still validate each target
application separately. A standards-compliant driver cannot guarantee that a
closed application will select a node, accept its formats, or use the portal.

## 2. What “proper professional driver” means

The implementation is complete only when all of the following are true:

- The sensor, actuator, CSI receiver, ISP, firmware, and capture queues have a
  coherent media-controller graph.
- Every application-facing video node is registered by the real capture driver,
  not by `v4l2loopback` or a userspace copier.
- The V4L2 userspace ABI is implemented honestly: capabilities, format
  negotiation, frame intervals, controls, polling, streaming, timestamps,
  sequence numbers, payload sizes, and errors are correct.
- MMAP works as the broad compatibility baseline. DMABUF is supported where the
  hardware and allocator model make it correct; it is an optimization, not a
  substitute for ordinary V4L2 capture.
- Runtime PM, suspend/resume, firmware loading, stream start/stop, failed-start
  unwind, remove, and unbind are deterministic.
- Sensor controls use standard V4L2 controls with documented units and ranges.
  3A algorithms live in libcamera/IPA or another userspace camera framework,
  not in an ad-hoc kernel application layer.
- The libcamera pipeline handler can discover the actual graph, configure it,
  queue requests, import/export buffers, expose camera properties, and return
  useful metadata.
- PipeWire/WirePlumber can expose the physical cameras through the libcamera
  monitor without duplicate raw nodes or a synthetic loopback endpoint.
- Installation, firmware provenance, module provenance, security policy,
  logging, tests, and upstream documentation are reproducible.

The Linux kernel uses “bridge driver” as a legitimate media-subsystem term for
the component that connects sub-devices and capture endpoints. That term must
not be confused with this project’s prohibited **application-compatibility
bridge**, which is the C service plus `v4l2loopback`.

## 3. Current project baseline

The repository is an out-of-tree IPU4P overlay, not yet a complete upstream
camera stack.

### Proven or partially proven

- Hardware target is one Surface Pro 7 on Fedora with
  `6.19.8-3.surface.fc43.x86_64`.
- The IPU4P media device currently reports driver `intel-ipu6` and model
  `ipu4p`.
- Raw V4L2 capture has succeeded for the front OV5693 and rear OV8865 paths.
- Fedora libcamera Simple + SoftISP has produced processed captures from both
  sensors through the current installed stack.
- The current IPU4P video nodes are raw/Bayer-oriented capture nodes; they are
  not yet a conventional YUYV/MJPEG conferencing-camera interface. The C
  bridge currently supplies conversion, scaling, and JPEG/YUYV output through
  `v4l2loopback`.
- `linux-6.19.8/drivers/media/pci/intel/` contains the IPU parent, ISYS, PSYS,
  CSI-2, queue, DMA, firmware, and CSS code used by the overlay.
- The in-tree OV5693 source describes a 10-bit BGGR CSI-2 sensor with exposure,
  analogue/digital gain, horizontal/vertical flip, blanking, pixel-rate,
  link-frequency, and test-pattern controls.

### Not complete or not proven

- OV8865 driver source is external to this repository.
- The IR OV7251 probe fails on the validated unit.
- There is no project-owned IPU4P libcamera pipeline-handler or IPA source;
  current success depends on the distribution’s Simple + SoftISP path.
- The latest bridge-free PipeWire preview test enumerated the two physical
  libcamera cameras but produced black frames. Direct `cam` capture remained
  valid, so this is an unresolved PipeWire/libcamera integration defect rather
  than proof of a broken sensor stream.
- Snapshot, Zoom, Discord, browser, portal permissions, switching, reopen, and
  concurrent-consumer behavior are not all qualified.
- The current default installer packages and enables the C bridge and
  `v4l2loopback`; that is explicitly outside the target architecture.

Evidence is recorded in [`README.md`](../README.md),
[`docs/task8-camera-contract.md`](task8-camera-contract.md),
[`docs/bridge-free-camera-testing-20260917.md`](bridge-free-camera-testing-20260917.md),
[`libcamera/README.md`](../libcamera/README.md), and
[`docs/upstream-readiness.md`](upstream-readiness.md).

## 4. Architecture requirements

### 4.1 Kernel media graph

Model the physical pipeline as media entities and pads:

```text
OV5693 / OV8865 sensor subdev
        -> CSI-2 receiver / ISYS subdev
        -> IPU4P ISYS capture video node       (raw/debug path)
        -> IPU4P PSYS/ISP processing pipeline
        -> processed capture video node         (webcam path, if supported)
```

The exact entity and node names must be discovered from the live media graph;
`/dev/video0` and `/dev/video54` are not stable identities. The graph must
carry the negotiated media-bus code, width, height, lane count, link
frequency, routing, crop, and stream state across every connected pad.

Requirements:

- Use `v4l2_subdev` for sensors, actuators, CSI blocks, and other sub-devices.
- Initialize media pads and links through the Media Controller API. Do not use
  the old simple webcam graph helper for this multi-stage ISP pipeline.
- Use V4L2 async registration/notifiers for independently probed sensors,
  actuators, and firmware-described dependencies.
- Use firmware-node/ACPI graph data for endpoint, lane, clock, orientation,
  GPIO, regulator, and actuator relationships. Do not hard-code minor numbers
  or assume probe order.
- Reject incompatible sink/source formats at the graph boundary rather than
  allowing a later stream-on failure.
- Keep media links and source-change behavior consistent across probe, remove,
  suspend, resume, and failed stream startup.
- Expose sub-device nodes only when direct userspace configuration is an
  intentional part of the API. Otherwise let the capture/pipeline driver
  control sub-devices through kernel calls.

References: [V4L2 sub-device kernel API](https://docs.kernel.org/driver-api/media/v4l2-subdev.html),
[Media Controller core](https://docs.kernel.org/next/driver-api/media/mc-core.html),
[Media Controller userspace API](https://docs.kernel.org/userspace-api/media/mediactl/media-controller.html),
[V4L2 async API](https://docs.kernel.org/driver-api/media/v4l2-async.html), and
[V4L2 firmware-node API](https://docs.kernel.org/driver-api/media/v4l2-fwnode.html).

### 4.2 Sensor and actuator contract

Each sensor driver must:

- Probe through the actual ACPI/fwnode identity and verify the chip ID where
  possible.
- Use only firmware-described external clock rates and supported CSI-2 link
  frequencies.
- Advertise all supported modes, native array bounds, active crop, bus code,
  lane count, link frequency, pixel rate, and frame intervals.
- Use standard controls with stable units. For raw sensors this includes, where
  applicable, exposure in image lines, analogue gain, HBLANK, VBLANK, and
  PIXEL_RATE; orientation/rotation and writable flips should be added when
  hardware supports them.
- Mark controls that change Bayer layout with the appropriate control flag.
- Use runtime PM for clocks, regulators, GPIOs, register access, and streaming;
  do not use the deprecated `.s_power()` path in new code.
- Stop and restart only through coordinated pipeline stream operations; a
  sensor PM callback must not independently restart a pipeline.
- Bind the DW9719 actuator through its firmware/I2C identity and expose
  standard focus controls only after safe power, range, and failure behavior
  is proven.

The kernel sensor-driver requirements are summarized by the
[camera sensor driver guide](https://docs.kernel.org/driver-api/media/camera-sensor.html).
For libcamera integration, also satisfy the project’s
[sensor driver requirements](https://github.com/raspberrypi/libcamera/blob/main/Documentation/sensor_driver_requirements.rst),
especially timing controls, crop selection, orientation, and flip semantics.

### 4.3 V4L2 capture-node ABI

Every webcam-facing `video_device` shall implement a normal V4L2 capture
contract:

| Area | Required behavior |
|---|---|
| Capabilities | `V4L2_CAP_VIDEO_CAPTURE` or `_MPLANE`, `V4L2_CAP_STREAMING`, and accurate `device_caps`; add `READWRITE` only if it really works. |
| Discovery | Descriptive card/driver/bus information; stable physical identity comes from sysfs/udev/libcamera properties, not a hard-coded minor. |
| Formats | Enumerate only formats the complete pipeline can produce. The product profile should prioritize common webcam formats such as YUYV/NV12/RGB as the IPU4P path permits; raw Bayer is a specialist/debug format, not the generic webcam contract. |
| Sizes | Enumerate real supported sizes and intervals. Do not accept arbitrary sizes and fail later. |
| Negotiation | Implement `G_FMT`, `S_FMT`, and `TRY_FMT`; return the actual adjusted format. Reject format changes while buffers/streaming make them unsafe. |
| Frame intervals | Implement enumeration and negotiation with truthful intervals. A nominal 30 fps mode must produce advancing frames at a measured cadence. |
| Controls | Attach a `v4l2_ctrl_handler`; expose only controls with real hardware/algorithm semantics and correct ranges, defaults, inactive/grabbed state, and events. |
| Buffers | Use videobuf2. MMAP is the compatibility baseline; support DMABUF import/export only with correct ownership, plane sizes, cache synchronization, and lifetime handling. |
| Streaming | `REQBUFS`/`QBUF`/`DQBUF`/`STREAMON`/`STREAMOFF` must work repeatedly. On failure, every queued buffer is returned with the correct error state. |
| Metadata | Use monotonic, non-regressing timestamps; increment sequence numbers; set `bytesused`, `data_offset`, field, colorspace, and payload sizes correctly. Do not label raw metadata nodes as 3A metadata without a proven firmware contract. |
| Polling | `poll()`/`select()`/`epoll()` wake only when a buffer or event is actually available and handle disconnect/error paths. |
| Concurrency | Define whether a second opener, a second stream, and a format change are supported. Return `EBUSY` deterministically when hardware resources are exclusive. |
| Errors | Use standard errno values, rate-limit repeated hardware errors, and preserve actionable kernel logs without leaking sensitive frame data. |

The normative references are the [V4L2 capture interface](https://docs.kernel.org/userspace-api/media/v4l/dev-capture.html),
[V4L2 I/O methods](https://docs.kernel.org/userspace-api/media/v4l/io.html),
[V4L2 controls API](https://docs.kernel.org/userspace-api/media/v4l/control.html),
and [videobuf2](https://docs.kernel.org/driver-api/media/v4l2-videobuf2.html).

### 4.4 Queue and DMA design

Use videobuf2 for every streaming capture queue. Define and test:

- queue setup and minimum queued buffers;
- buffer preparation, plane sizes, strides, and alignment;
- DMA mapping/unmapping and IOMMU ownership;
- interrupt-safe completion and teardown;
- queueing before and after stream-on;
- stream-on rollback when sensor, CSI, firmware, or IPU setup fails;
- stream-off while buffers are active;
- process death and file-descriptor close;
- memory pressure and allocation failure;
- MMAP and, separately, DMABUF importer/exporter behavior.

The [vb2 API](https://docs.kernel.org/driver-api/media/v4l2-videobuf2.html)
defines the memory models and queue callbacks. The driver must not report a
buffer as usable merely because an interrupt occurred: the frame must have a
valid payload, advancing sequence/timestamp, and image content when the
hardware is expected to provide it.

### 4.5 Power, firmware, and lifecycle

Implement a single documented lifecycle:

```text
probe -> firmware available -> graph complete -> idle/runtime-suspended
  -> open -> configure -> queue -> stream-on -> frames
  -> stream-off -> unqueue -> close -> autosuspend
  -> system suspend/resume -> graph/firmware restored
```

Requirements:

- Load and validate `ipu4p_cpd.bin` through the kernel firmware mechanism;
  record the firmware name/version and keep redistribution policy explicit.
- Acquire runtime-PM references before register access and active streaming;
  release them on every success and failure path, including partial startup.
- Restore sensor, CSI, IPU, and firmware state after runtime/system resume.
- Ensure no interrupt, work item, DMA callback, media request, or open file
  uses freed state during remove/unbind.
- Make startup recovery finite and diagnosable. A retry may recover a known
  hardware quirk, but it must not hide an invalid frame-sync or queue contract.
- Preserve module vermagic, dependency metadata, PCI aliases, module license,
  and `MODULE_FIRMWARE` declarations.

References: [camera-sensor PM rules](https://docs.kernel.org/driver-api/media/camera-sensor.html),
[runtime PM](https://docs.kernel.org/power/runtime_pm.html), and
[kernel patch checklist](https://docs.kernel.org/process/submit-checklist.html).

## 5. Native application integration without a bridge

### 5.1 Direct V4L2 applications

This path is satisfied by a real processed capture node with conventional
formats, MMAP, sensible dimensions, stable frame intervals, and standard
controls. It is the only path that can work with a V4L2-only application using
the kernel driver alone.

It does not imply that all V4L2 applications will work. Each application may
reject multi-planar formats, raw Bayer, unusual strides, unsupported colorspace
values, missing controls, or exclusive access behavior.

### 5.2 libcamera applications

The IPU4P pipeline handler must:

- match the actual media device and sensor entities;
- acquire the media device exclusively for a camera session;
- identify sensors by graph/fwnode identity rather than minor numbers;
- configure sensor, CSI, ISYS, PSYS/ISP, and output formats as one transaction;
- export or import frame buffers and queue libcamera requests;
- map V4L2 controls into libcamera controls;
- provide camera location/orientation and immutable properties;
- provide frame metadata needed by IPA and applications;
- handle camera acquisition conflicts and shared IPU4P resources;
- stop and release every device on camera release, error, or process death.

The libcamera pipeline-handler guide describes this exact role: it uses
V4L2/Media Controller to configure device-specific pipelines and exposes
streams, controls, requests, and buffers through libcamera
([pipeline handler guide](https://docs.libcamera.org/master/guides/pipeline-handler.html)).

The IPA should implement AE/AGC/AWB/AF only after the sensor controls,
timestamps, frame duration, exposure/gain application, and metadata are
correct. Do not compensate for broken driver timing or stale buffers in the
IPA.

### 5.3 PipeWire and desktop portals

Enable the native PipeWire libcamera monitor and ensure that the physical
cameras appear as `Video/Source` nodes with meaningful location and camera
properties. WirePlumber supports separate V4L2 and libcamera monitors and can
arbitrate duplicate hardware; complex ISP cameras generally require the
libcamera monitor ([WirePlumber camera configuration](https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/video.html)).

For sandboxed applications, the XDG Camera portal grants access to a PipeWire
remote; it does not hand the application a `/dev/video*` file descriptor
([Camera portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Camera.html)).
The stack must therefore validate both unsandboxed and Flatpak-style access.

The native PipeWire path must not hide all physical cameras merely to make a
loopback list look clean. Hide raw/debug nodes only through a documented
WirePlumber policy when their exposure would create duplicate or unusable
camera entries, while leaving the libcamera source visible.

### 5.4 Compatibility matrix

| Consumer | What the driver can guarantee | What must be separately implemented/verified |
|---|---|---|
| `v4l2-ctl`, FFmpeg V4L2, GStreamer `v4l2src` | Correct V4L2 ABI and capture node | Their accepted formats, caps negotiation, and multi-planar behavior |
| `cam` / libcamera apps | Correct graph and libcamera-facing controls | A matching pipeline handler, IPA, stream configuration, and metadata |
| PipeWire/WirePlumber | Physical V4L2/libcamera source availability | Monitor selection, node policy, permissions, format negotiation, fan-out |
| XDG Camera portal | Nothing directly | Portal backend, permissions, PipeWire camera role, application portal use |
| GNOME Snapshot | Nothing directly | GStreamer/PipeWire/libcamera startup and usable preview/capture |
| Chromium/browser | Nothing directly | Browser backend, portal permissions, WebRTC constraints |
| Zoom/Discord | Nothing publicly guaranteed | Exact shipped client behavior, formats, permissions, preview, switching, relaunch |

Application success is therefore measured by live preview/capture in the named
application, not by node enumeration or a raw `v4l2-ctl` probe alone.

## 6. Explicitly rejected designs

The following are useful experiments or temporary fallbacks, but are not part
of the professional no-bridge target:

- `v4l2loopback` endpoints fed by GStreamer/libcamera.
- The project C service in `cbridge/` that copies frames into named loopbacks.
- `libcamera-v4l2`/`libcamerify` `LD_PRELOAD` compatibility wrapping.
- A kernel module that creates fake `Surface Camera (front/back)` nodes without
  owning the physical stream and buffer lifecycle.
- Hard-coded `/dev/videoN` or `/dev/v4l-subdevN` identifiers.
- A private ioctl or application-specific kernel API to compensate for missing
  standard V4L2 behavior.
- Treating raw BG10/Bayer or IPU metadata nodes as ready-made conferencing
  video without a documented conversion and metadata contract.

libcamera itself documents its V4L2 compatibility layer as an injected wrapper
that traps camera accesses and emulates high-level V4L2 devices
([libcamera feature requirements](https://docs.libcamera.org/master/feature_requirements.html)).
That is precisely the class of compatibility mechanism excluded by this
specification.

## 7. Phased implementation plan

### Phase 0 — Freeze the contract and inventory the hardware

1. Capture fresh `lspci`, `uname -r`, `modinfo`, firmware hashes, ACPI/fwnode
   graph data, `media-ctl -p`, `v4l2-ctl --all`, and module dependency output.
2. Build a per-sensor table for OV5693 and OV8865: chip ID, lanes, link rate,
   modes, crop, controls, frame intervals, orientation, and actuator.
3. Identify which IPU4P entities are raw ISYS outputs, which are PSYS/ISP
   outputs, and which output formats are actually supported by the firmware.
4. Decide whether an application-friendly processed V4L2 node is feasible in
   the kernel path. If not, formally select “native libcamera + PipeWire” as
   the product webcam path while retaining raw V4L2 for diagnostics.
5. Treat the absence of a public IPU4P programming specification as a project
   risk. Keep register-level and firmware behavior claims tied to captured
   evidence or source provenance rather than presenting reverse-engineered
   values as a hardware standard.

**Exit gate:** the graph and format inventory are reproducible after reboot,
with no hard-coded minor-number assumptions.

#### Phase 0 repository implementation

The repository provides `tests/native-inventory.sh --live`, also invoked by
`tests/camera-suite.sh --live`. It creates a report directory and dynamically
walks `/dev/media*` and `/sys/class/video4linux/` rather than assuming any
`videoN`, `mediaN`, or `v4l-subdevN` minor. For each discovered node it records
the sysfs identity, udev properties when available, V4L2 capabilities and
formats, sub-device controls and media-bus codes, media-controller graphs,
loaded-module metadata, module firmware requests and standard firmware-file
presence. It also records read-only `cam`, GStreamer `libcamerasrc`, PipeWire,
and WirePlumber status when those tools are installed. Missing hardware or
tools are reported as `SKIP`; the inventory never loads modules, changes graph
links, restarts services, or claims a usable image.

The static contract is covered by
`tests/task28-native-inventory-static.sh` and the existing shell suite. The
inventory does not replace the hardware-only gates: clean advancing frames,
non-black scene data, controls, suspend/resume, native libcamera preview,
PipeWire portal access, and named-application preview/capture still require a
prepared Surface Pro 7. The project C bridge and `v4l2loopback` remain
explicitly supported as fallback/test tooling until those native gates pass;
they are not evidence that the no-bridge target is complete.

### Phase 1 — Make the kernel driver production-safe

1. Rebase the overlay against the exact target kernel and separate generic
   fixes from the Surface Pro 7 compatibility quirk.
2. Convert all probe/graph dependencies to fwnode/async registration and
   document every ACPI-specific exception.
3. Audit every video queue and sub-device operation for vb2 ownership,
   locking, DMA lifetime, `bytesused`, timestamps, and failed-start unwind.
4. Complete sensor controls and selection/timing semantics; add OV8865 as a
   real driver dependency with the same contract.
5. Remove unsafe stream-start retries or bound them around a demonstrated
   hardware workaround with explicit logs and tests.
6. Add runtime PM and suspend/resume tests for sensor, CSI, IPU, firmware, and
   actuator state.

**Exit gate:** repeated front/rear capture, stream restart, process death,
runtime suspend/resume, system suspend/resume, and error injection leave no
wedged device, stale frame, invalid payload, or leaked PM reference.

### Phase 2 — Implement the native camera framework path

1. Add an IPU4P libcamera pipeline handler, initially with one sensor and one
   stream, then front/rear resource sharing.
2. Add the sensor helpers/IPA only after controls and frame metadata are
   verified. Start with deterministic exposure/gain configuration; add AE/AGC,
   AWB, and autofocus incrementally.
3. Support common viewfinder configurations and prove format conversion and
   buffer import/export without copying through a loopback node.
4. Make camera IDs, locations, orientation, and physical graph identities
   stable across reboot and probe-order changes.

**Exit gate:** `cam --list`, front/rear `cam` captures, control changes,
switching, release/reopen, and concurrent-resource failures are deterministic.

### Phase 3 — Native PipeWire/portal integration

1. Enable and qualify WirePlumber’s libcamera monitor for this device.
2. Remove the project’s loopback service and raw-node policy from the test
   environment; do not change the kernel driver to satisfy a WirePlumber list.
3. Verify that the PipeWire source produces non-black, advancing frames in
   `gst-launch`, with requested common formats and frame rates.
4. Verify portal permission and PipeWire remote access for a Flatpak test app.
5. Measure one and two consumer cases. Document whether the hardware is
   exclusive, and whether PipeWire copies/converts frames for fan-out.

**Exit gate:** native PipeWire preview is stable for 30 minutes, survives
   source stop/start and WirePlumber restart, and produces image content—not
   merely a listed node.

#### Phase 3 repository implementation

The repository provides an opt-in native profile at
`wireplumber/60-sp7-ipu4-native.conf` and a read-only check at
`tests/native-pipewire-validation.sh`. The check discovers current
`Video/Source` nodes with libcamera properties through `pw-dump`, requests a
bounded `pipewiresrc` preview with common RGB caps, and requires nonzero,
changing frame data. It reports `SKIP` when the PipeWire tools, libcamera
monitor, or native nodes are unavailable, and `FAIL` when a discovered source
cannot preview or emits black/static frames. It never changes services, links,
modules, or the bridge/loopback fallback. The optional profile hides only raw
debug V4L2 nodes and enables the physical libcamera monitor; it is not installed
by the default bridge flow. On the validated host, native PipeWire preview is
still unqualified and has previously produced black frames, so no portal or
desktop-application success is implied.

### Phase 4 — Application qualification

The concrete, repeatable contract is in
[`docs/application-qualification.md`](application-qualification.md). Execute
it separately for GNOME Snapshot, Chromium/WebRTC, Zoom Linux, Discord Linux,
a direct V4L2 client, and a native `cam` client. The record must identify the
exact package/runtime and sandbox state, the selected physical camera by
stable graph/libcamera identity, preview movement and measured frame rate,
lens-cover/content behavior, front/rear switching, close/reopen, relaunch,
portal permission results, and bounded kernel/PipeWire logs.

Every row has one explicit result: `PASS`, `FAIL`, `NOT TESTED`, or
`ENUMERATED-ONLY`. Only `PASS` is application qualification. Enumeration, device
open, raw capture, a PipeWire node, or a C-bridge/`v4l2loopback` result can
populate evidence or a diagnostic row, but cannot promote an application to
`PASS`.

**Exit gate:** the release support statement names only applications whose
own row has observed moving preview and capture with the required lifecycle,
camera-switching, permission, and log evidence. The current repository has no
application `PASS` claim.

### Phase 5 — Packaging and upstream readiness

1. Reduce the overlay to the smallest kernel patch series; move generic code
   toward mainline subsystem locations.
2. Add Kconfig/Makefile, firmware, ACPI/DT binding, ABI, and driver
   documentation as appropriate.
3. Run `checkpatch.pl`, sparse/smatch, warning-clean builds, relevant `=y/=m/=n`
   configurations, `v4l2-compliance`, media graph tests, and fault injection.
4. Publish exact kernel/firmware/module provenance and reproducible install and
   uninstall steps.
5. Submit patches using the Linux media maintainers’ process, with hardware
   details, supported modes, firmware dependencies, power-management results,
   compliance output, and known limitations.

The [kernel submission checklist](https://docs.kernel.org/process/submit-checklist.html)
and [patch submission guide](https://docs.kernel.org/process/submitting-patches.html)
are acceptance criteria, not optional release polish.

## 8. Validation specification

### Static and build checks

```sh
./tests/camera-suite.sh
for script in *.sh libcamera/*.sh scripts/*.sh tests/*.sh; do
    sh -n "$script"
done
git diff --check
```

For the kernel tree, add the normal kernel checks plus media-specific builds:

```sh
make -C "$KDIR" M="$PWD/linux-6.19.8/drivers/media/pci/intel" W=1 modules
scripts/checkpatch.pl --strict <patch-series.patch
make C=1 CF='-D__CHECK_ENDIAN__' ...
```

The exact commands must be adapted to the prepared target kernel and overlay
layout; never claim a mainline build from an out-of-tree module build alone.

### Graph and ABI checks

For every boot and both sensors:

```sh
media-ctl -p
v4l2-ctl --list-devices
v4l2-ctl --all --device /dev/videoX
v4l2-ctl --list-formats-ext --device /dev/videoX
v4l2-compliance --device /dev/videoX
```

The test harness must resolve `videoX`, `mediaX`, and `v4l-subdevX` from
sysfs/media entities, then verify the resolved entity names and topology.

### Stream correctness checks

For each supported mode and memory model:

- queue enough buffers, stream for at least 300 frames, and confirm advancing
  sequence numbers and monotonic timestamps;
- hash or inspect a sample of frames to prove changing, non-black scene data;
- verify `bytesused <= plane length` and correct stride/sizeimage;
- stop/restart at least 20 times;
- kill the client during streaming and verify automatic cleanup;
- alter supported controls while idle and, where documented, while streaming;
- switch front/rear only after clean release of the previous camera;
- run with WirePlumber stopped, then with it active, to separate kernel and
  session-manager failures.

### Power and fault checks

- runtime suspend/resume between every other stream;
- system suspend/resume with no open camera and with an orderly closed stream;
- firmware absent, invalid, or delayed;
- sensor probe defer and actuator absence;
- I2C error during control application;
- CSI frame-sync error, dropped frame, DMA error, and queue starvation;
- allocation failure and failed ISP/PSYS startup;
- module unload/remove only after all users are closed.

### Desktop checks

- native PipeWire source with libcamera monitor enabled;
- no synthetic `/dev/video60`/`/dev/video61` endpoints;
- portal permission grant and denial;
- Flatpak and unsandboxed clients;
- one client, two clients, source restart, WirePlumber restart, and full
  application relaunch;
- actual preview movement and lens-cover tests for front and rear cameras.

## 9. Acceptance criteria and support statement

The project may call the driver **native and production-ready for the stated
target** only when Phases 0–4 pass and the support statement names exact
kernel, firmware, sensor-driver, libcamera, PipeWire, and application versions.

It may call the kernel portion **V4L2-compliant** only after the real capture
node passes the applicable V4L2/media compliance suite and the frame/content,
power, lifetime, and error tests above.

It must not claim “works with Linux webcam applications” from any of the
following alone:

- `/dev/video*` enumeration;
- a successful `STREAMON` with no valid advancing frame;
- a raw Bayer capture;
- a libcamera `cam` capture while PipeWire is disabled;
- a PipeWire node that produces black frames;
- a loopback endpoint fed by the C bridge;
- a single successful run in one application.

The final release should make the bridge optional or remove it entirely only
after native PipeWire and the named application trials pass. Until then, the
bridge is a fallback test tool, not evidence that the driver satisfies this
specification.

## 10. Primary research sources

- [Linux V4L2 capture interface](https://docs.kernel.org/userspace-api/media/v4l/dev-capture.html)
- [Linux V4L2 I/O methods](https://docs.kernel.org/userspace-api/media/v4l/io.html)
- [Linux Media Controller userspace API](https://docs.kernel.org/userspace-api/media/mediactl/media-controller.html)
- [V4L2 sub-device API](https://docs.kernel.org/driver-api/media/v4l2-subdev.html)
- [Media Controller kernel API](https://docs.kernel.org/next/driver-api/media/mc-core.html)
- [V4L2 async notifier API](https://docs.kernel.org/driver-api/media/v4l2-async.html)
- [V4L2 firmware-node parsing](https://docs.kernel.org/driver-api/media/v4l2-fwnode.html)
- [Camera sensor driver guidance](https://docs.kernel.org/driver-api/media/camera-sensor.html)
- [V4L2 controls framework](https://docs.kernel.org/driver-api/media/v4l2-controls.html)
- [Videobuf2 API](https://docs.kernel.org/driver-api/media/v4l2-videobuf2.html)
- [Runtime power management](https://docs.kernel.org/power/runtime_pm.html)
- [Kernel patch submission checklist](https://docs.kernel.org/process/submit-checklist.html)
- [libcamera architecture](https://docs.libcamera.org/master/)
- [libcamera pipeline-handler guide](https://docs.libcamera.org/master/guides/pipeline-handler.html)
- [libcamera feature requirements and V4L2 compatibility layer](https://docs.libcamera.org/master/feature_requirements.html)
- [PipeWire factory names](https://pipewire.pages.freedesktop.org/pipewire/group__spa__names.html)
- [WirePlumber camera configuration](https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/video.html)
- [XDG Camera portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Camera.html)
