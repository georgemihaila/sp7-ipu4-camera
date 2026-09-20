# OV7251 Howdy capture qualification and preparation

Date: 2026-09-20  
Status: **prepared offline; live hardware qualification is not claimed**

This report records the source, runtime baseline, offline gates, rollback
design, and the remaining authorization boundary for protected Howdy capture.
PAM, authselect, GDM, login, and pre-login activation were not changed.

## Exact revisions and runtime provenance

| Item | Value |
|---|---|
| Repository baseline before this preparation | `b8fe00d9438a2f36bc30fd1ea2074b8dbdf42b22` |
| Howdy adapter-test commit | `0d4592c6ba00bba3321e96f4d7ae6b784b088395` |
| Receiver lifecycle-diagnosis test commit | `685884d` |
| Route lifecycle preparation commits | `f075599`, rollback hardening `823dc34` |
| Howdy recorder revision | `d3ab99382f88f043d15f15c1450ab69433892a1c` |
| Running kernel | `6.19.8-3.surface.fc43.x86_64` |

The read-only runtime baseline was collected with `git rev-parse HEAD`,
`modinfo`, `sha256sum "$(modinfo -n MODULE)"`, `/proc/modules`, the relevant
`/sys/module/*/parameters/*` files, and `systemctl is-enabled/is-active`.
Observed module provenance was:

| Module | Loaded/installed object | SHA-256 | Vermagic |
|---|---|---|---|
| `ov7251` | `/lib/modules/6.19.8-3.surface.fc43.x86_64/updates/extra/ov7251.ko` | `f921aecddd2be68b0662534e9adb05ce6e07be0a6c9681684c8cf0ae8dffdd3f` | `6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload` |
| `intel_ipu4p_isys` | `/lib/modules/6.19.8-3.surface.fc43.x86_64/updates/extra/intel-ipu4p-isys.ko` | `546d80d5c692e0b394b56f4771fbf244bb9427419f6126405fe766fc9b1eaf7b` | same |
| `ipu_bridge` | `/lib/modules/6.19.8-3.surface.fc43.x86_64/kernel/drivers/media/pci/intel/ipu-bridge.ko.xz` | `36a2e7a2ef4fe718e971c39362536b32b69cdc028554bed0978663f1ef151af9` | same |

The loaded parameters were `ov7251.experimental_strobe_output=Y`,
`ov7251.strobe_diagnostics=Y`, and
`intel_ipu4p_isys.debug_capture_links=N`. The user RGB bridge was active at
baseline. `sp7-camera-howdy-preflight.service` was disabled and inactive; the
new route lifecycle unit was not installed or enabled.

## Prepared source and rollback boundary

The separate `cbridge/sp7-camera-ir-route` utility now:

1. discovers `/dev/media*` and the OV7251, source-6 CSI, and capture entities
   by name;
2. snapshots the two target link enabled bits in a root-owned state file;
3. enables the sensor-to-CSI and CSI-to-capture links without unloading a
   module, changing a sensor register, stopping RGB, or selecting
   `/dev/video62`; and
4. restores the saved states in reverse pipeline order and removes the state
   file only after restoration succeeds.

`systemd/system/sp7-camera-howdy-route.service` and
`scripts/ir/install-route-lifecycle.sh` provide a future pre-login lifecycle,
but installation leaves the unit disabled and stopped. Authentication does
not invoke the route utility. If a future authorized session cannot restore a
link, the state file is deliberately retained for diagnosis instead of being
silently discarded.

## Offline results

The following completed without camera access, module replacement, service
restart, or PAM/GDM changes:

| Gate | Result | Evidence |
|---|---|---|
| Pinned Howdy adapter contract | PASS | `tests/task38-howdy-adapter-static.sh`; 12 fresh `SP7IRF01` frames, BGR conversion, unsupported-control rejection, busy/error cleanup, and deadline bounds |
| Protected helper protocol | PASS | Existing `tests/task36-auth-capture-static.sh` and protocol test |
| Receiver lifecycle evidence boundary | PASS | `tests/task39-ipu4p-lifecycle-diagnosis-static.sh`; no unproven kernel fix was added |
| Graph-discovered route lifecycle | PASS | `tests/task40-ir-route-static.sh`; strict C compilation and help-path validation |
| Shell/Python syntax and whitespace | PASS | `bash -n`, `sh -n`, Python compilation, and `git diff --check` |

The complete static suite is the required final offline command:

```sh
./tests/camera-suite.sh --static
```

Its static checks do not prove a live route, changing frames, illumination,
face detail, enrollment, matching, cleanup, or spoof resistance.

## Live qualification status

The following were **not run in this preparation** because they require a
separate authorization for hardware tests, module replacement, and possible
RGB/service disruption:

* receiver-only startup with strobe disabled, 20 reopen cycles, and the
  600-second capture qualification;
* fresh changing frames with valid metadata, receiver-fatal-error absence,
  bounded retry cancellation, and successful kernel cleanup;
* fixed-control off/on/off strobe observations with an independent
  IR-sensitive observer, first for three clean cycles and then ten cycles;
* protected headless diagnostic, fresh enrollment, and separate matching;
* normal light, darkness, orientation, glasses, login-distance, no-face,
  wrong-person, busy, startup failure, stale, malformed, truncated, timeout,
  cancellation, and reopen cases.

The previous enabled comparison remains limited evidence: it showed one
positive fixed-scene off/on difference and register cleanup, while the later
repeatability attempt failed before completing its third cycle. It does not
qualify repeatable illumination or receiver cleanup. The documented
`verify_stream_start()` OOPS followed by `v4l2_release` wait remains a strong
self-deadlock hypothesis, not a proven subdevice-lifetime root cause.

## Authorized-session acceptance and rollback

Before any future live session, record source revision, module paths and
hashes, vermagic, loaded parameters, media graph, bridge/service state, and
the target application's warmed RGB preview/capture. Run receiver diagnosis
with `experimental_strobe_output=0`; do not reuse a script that silently
replaces modules or stops RGB. Only after receiver cleanup passes should the
strobe comparison run with fixed exposure, gain, scene, format, and processing.

For the later Howdy compatibility session, keep `recording_plugin=sp7_ir`, the
protected helper and exclusive lock, `SP7IRF01` freshness checks, the three-
second helper budget inside Howdy's five-second attempt deadline, and clean
12-frame exhaustion as no-match. Unsupported exposure/FPS controls must be
configured at the capture layer, never reported as if Howdy applied them.

On every failure, stop the capture attempt, restore the route through the
separate lifecycle tool, verify the state file is gone, verify the media graph
and module provenance, and revalidate warmed moving RGB preview and capture.
Do not proceed to PAM, authselect, GDM, or pre-login activation until every
compatibility gate passes. Biometric frames and enrollment data remain outside
Git.
