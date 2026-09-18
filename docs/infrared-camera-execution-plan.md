# Surface Pro 7 infrared camera: research and execution plan

Research date: 2026-09-18. Repository: `/home/george/repos/sp7-ipu4-camera`.

## Objective and handoff instructions

Make the separate OV7251 infrared sensor produce reliable, visibly changing monochrome images on this Surface Pro 7, establish whether its built-in infrared illumination can be controlled correctly, and expose a reproducible capture/preview path. Preserve the working front and rear RGB camera bridge. Face authentication is a later, separately authorized project.

Execute the stages below in order. Use GPT-5.6 Luna agents at high effort for bounded driver/source investigations and medium effort for documentation/test tooling. Delegate independent read-only investigations in parallel; serialize hardware access and shared driver edits. Do not interrupt agents merely because they are slow. Commit each completed, validated subtask; do not push. Do not claim the overall goal is complete when only probe or enumeration works.

Before changes, inspect current instructions, branch, working tree, running kernel, and service state. Create an isolated `codex/ov7251-ir-bringup` branch/worktree from the current intended base; preserve unrelated changes. Working trees isolate source, not running hardware: only one agent may control the camera stack. This plan authorizes no hardware changes by itself; when asked to execute it, perform reversible work within that request. Obtain explicit permission for reboot, suspend tests, boot selection changes, or PAM changes. Prepare the exact change and rollback before requesting a remaining approval. Do not install Howdy as a substitute for bring-up.

## What was actually verified

On the research date, checkout `main` was clean at `d8bb67f`; the running kernel was `6.19.8-3.surface.fc43.x86_64`. Refresh both at execution time.

- Current-boot logs identify `INT347E:00` as a supported sensor, then record dummy `vdddo`, `vddd`, and `vdda` regulators and `ov7251_write_reg: write reg error -110: reg=103, val=1`, followed by `error during global init` and failed probe.
- The I2C device `/sys/bus/i2c/devices/i2c-INT347E:00` exists but has no bound-driver symlink. The media graph has the RGB sensors and no OV7251 sensor entity. Generic IPU video nodes are not proof of an IR sensor.
- `modinfo ov7251` resolves to the distribution module under `/lib/modules/6.19.8-3.surface.fc43.x86_64/kernel/drivers/media/i2c/ov7251.ko.xz`. The repository module manifest contains IPU modules, not OV7251.
- `sp7-camera-bridge.service` was active. This research did not stop it, reprobe sensors, load modules, alter power controls, or capture images.
- `linux-6.19.8/drivers/media/pci/intel/ipu-bridge.c` contains `IPU_SENSOR_CONFIG("INT347E", 1, 319200000)`. Here `1` is the number of listed link frequencies, **not a measured CSI lane count**. Lane count and port must be established from firmware and the generated endpoints.
- The shared Intel source already handles `MEDIA_BUS_FMT_Y10_1X10`: inspect `ipu-isys-csi2.c`, `ipu-isys-csi2-be-soc.c`, `ipu-isys-video.c`, and `ipu-isys-subdev.c`. Different capture paths have different storage descriptions; existing entries do not prove correct end-to-end packing.

The failure precedes CSI streaming and userspace. Power, clock, reset, ACPI dependencies, and I2C transactions are the first investigation area. **None is yet a proven root cause.** Dummy-regulator messages alone do not prove missing physical power: firmware may own the rails. The historical boot log's wall-clock dates are unusual; retain boot ID and monotonic timestamps when gathering fresh evidence.

## Research context

The [linux-surface camera matrix](https://github.com/linux-surface/linux-surface/wiki/Camera-Support) still marks SP7 OV7251 unsupported. It also lags this repository's RGB results, so treat it as project status rather than proof of impossibility. The [IPU4 discussion](https://github.com/linux-surface/linux-surface/discussions/1353) contains SP7 hardware reports, including the same IR I2C failure; reports from this project are not independent confirmation.

Use the [upstream OV7251 driver](https://github.com/torvalds/linux/blob/master/drivers/media/i2c/ov7251.c) and [INT3472 platform code](https://github.com/torvalds/linux/tree/master/drivers/platform/x86/intel/int3472) as comparison sources, but obtain the **exact distribution source and patch set matching the loaded module** first. Record repository commits, package versions, and relevant diffs; `master` URLs are discovery references, not reproducible build inputs.

[Kernel sensor-driver guidance](https://docs.kernel.org/driver-api/media/camera-sensor.html) supports firmware-derived clocks and runtime-PM ownership of sensor resources. Do not copy a new helper into an older target without checking API availability. [linux-enable-ir-emitter](https://github.com/EmixamPP/linux-enable-ir-emitter/blob/master/README.md) controls UVC cameras; it is not a demonstrated solution for this MIPI/IPU device.

Concrete leads from [discussion #1353](https://github.com/linux-surface/linux-surface/discussions/1353), to verify against this unit: IR SSDB reports port 6, one lane, 19.2 MHz MCLK and ICL2; the reported control resources are enable pin 99 and reset pin 74, with no third LED resource. The reported receiver offset is `0x6c000`. These firmware pin numbers are not portable Linux GPIO numbers. Prioritize checking whether the installed sensor driver consumes the reset resource correctly; this is a hypothesis, not a diagnosed fix. Do not hard-code these values before local confirmation.

Important naming trap: [upstream INT3472 discrete.c](https://github.com/torvalds/linux/blob/master/drivers/platform/x86/intel/int3472/discrete.c) deliberately maps OV7251 firmware RESET to the sensor connection named `enable`. A driver requesting only `enable` therefore does not establish that reset is ignored. Verify the matching distribution mapping, polarity and actual power-enable regulator consumer names before adding any second GPIO. Upstream distinguishes privacy LED type `0x0d` from strobe/IR-flood type `0x02`; inspect registered LED devices and the real firmware mapping rather than equating an activity LED with illumination.

## Highest-priority experiment: the newly published supply-name fix

An [August 31 patch](https://lkml.iu.edu/2608.3/13473.html), [reported committed to media.git/next on September 5](https://www.mail-archive.com/linuxtv-commits@linuxtv.org/msg49522.html), maps INT347E POWER_ENABLE to regulator consumer `vdda` instead of the generic `avdd`. Its author reports working probe, illumination and 640×480/30 fps on Surface Pro 7+ (IPU6). This is strong adjacent-hardware evidence, **not a tested SP7/IPU4P fix**.

Make this the first Stage 1 comparison and, if applicable, the first Stage 3 experiment. Check whether the exact target source already contains the mapping and whether this unit actually has that power-enable resource. If the mapping is absent and the resource matches, backport the small upstream change with attribution into INT3472, preserving RESET-to-`enable`. Build/test the platform module first; an OV7251 sensor-code change may be unnecessary. Resolve and record the actual upstream commit before backporting; do not infer mainline inclusion from a mailing-list announcement.

Pass criteria: `vdda` resolves to the real gated supply, enabling it is observed, the reset write and chip-ID read succeed, and probe registers the sensor. Remaining dummy `vddd`/`vdddo` supplies may be appropriate for a single-gated-rail design. If the patch is already present or fails these checks, proceed through the diagnostic branches below; do not repeatedly apply it or declare the cause proven from matching symptoms.

## Stage 0 — Capture a baseline and recovery procedure

1. Create `docs/ir-baseline.md` recording kernel/config, firmware/BIOS revision, package versions, git revisions, installed module paths/hashes/vermagic, media topology and current bridge configuration. Record the module binary on disk separately from evidence of what is resident.
2. Collect current-boot monotonic kernel logs, ACPI/I2C supplier links and binding state, and media/V4L2 inventory. Inspect all relevant media devices rather than assuming `/dev/media0`.
3. Identify the existing baseline tests for front and rear, and record actual preview behavior in an installed client before disruptive work. Coordinate exclusive capture access; restore the original service state afterward.
4. Document recovery using the exact original modules and configuration. Avoid overwriting distribution module files; retain backups and an installation manifest for any experimental override. Account for Secure Boot/signing and initramfs copies if relevant.
5. Keep raw captures, complete ACPI dumps, Windows binaries, and bulky logs outside tracked source; commit a concise evidence summary and reproducible commands. Review staged files explicitly.

Acceptance: a second agent can identify the baseline and restore it without guessing. Commit: `docs: record OV7251 baseline and recovery procedure`.

## Stage 1 — Establish the actual firmware and driver contract

Run two independent read-only investigations: exact driver/source provenance; ACPI/resource topology. The orchestrator combines their findings before choosing a patch.

1. Obtain matching Fedora/linux-surface source, configuration and patches. Trace `ov7251_probe`, its first register write, power-on sequence, clock acquisition, GPIO request/polarity, regulator handling, and error unwinding. Verify that the logged register is hexadecimal `0x0103` in this version and identify its meaning from source/documentation.
2. Dump and disassemble ACPI tables using `acpidump`/`iasl` in a retained evidence directory. Follow `INT347E`, its I2C SerialBus resource/address/controller, `_DEP`, `_CRS`, `_DSM`, `_DSD`, `_PR0`, `_PS0`/`_PS3` when present, SSDB and associated `INT3472` device. Do not execute arbitrary AML methods.
3. Build a resource table: resource; firmware owner; Linux provider; consumer name; polarity/rate; current binding; supporting evidence. Include reset/powerdown, clock enable and frequency, regulator/enable lines, and any emitter/strobe resources. Distinguish sensor supplies, camera indicator LED, and IR illuminator.
4. Compare with working RGB resource plumbing on this host, without assuming shared wiring or polarity. Determine whether INT3472 is discrete GPIO or another control type; do not presume a TPS68470 is present.
5. Inspect `ipu-bridge.c` SSDB parsing and generated endpoint properties: CSI port, data lanes, external clock, link frequency and endpoint pairing. Verify whether providers defer correctly and whether dependencies are ready before sensor access.

Acceptance: a written, evidence-backed hypothesis table, ordered by likelihood and a one-variable experiment for each. If ownership remains unknown, say precisely which resource blocks diagnosis; do not invent GPIO numbers. Commit: `docs: map OV7251 firmware resources and probe sequence`.

## Stage 2 — Build a controlled probe experiment

1. Add minimal optional diagnostics at meaningful boundaries: provider readiness, resource acquisition, power/clock/reset sequence, delay, first transaction and exact transfer result. Preserve original errno and cleanup. Do not flood every register write.
2. Build the smallest affected module from matching source, with a reproducible script/patch and pinned provenance. For the priority supply-mapping experiment this is INT3472; build a replacement OV7251 module only if sensor diagnostics or a sensor change are needed. The existing `scripts/build-modules.sh` only builds the Intel overlay; it will not rebuild this sensor. If the actual fix is in INT3472, build and track that module separately too.
3. Verify target configuration, symbol versions, vermagic and signing requirements. Record module SHA-256 and an observable build identifier so a successful test can be attributed to the loaded experiment. `modinfo` on disk alone is insufficient after a replacement.
4. Prepare exact install/load/rollback commands. Stop camera consumers and temporarily stop the bridge only for the controlled experiment, retain its prior state, and restore it on success or failure. Unload only the minimum dependency set; no forced unloads or unrelated driver removal.
5. Retry binding only the exact OV7251 device after verified dependencies, with bounded attempts and fresh logs. Do not use broad `i2cdetect` scans, arbitrary `i2cset`, guessed GPIO toggles, or permanently forced power as a fix.

Acceptance: reproducible baseline failure with diagnostic attribution, followed by one-variable experimental results. Commit useful diagnostic/build infrastructure separately from the eventual fix. Temporary failed guesses should not become production changes.

## Stage 3 — Fix probe at the layer that owns the defect

Choose the branch supported by Stage 1–2 evidence:

| Evidence | Appropriate change | Required proof |
| --- | --- | --- |
| Missing/wrong supplier mapping or dependency | Correct INT3472/ACPI resource mapping or probe ordering | Correct owner is bound; genuine dependency absence defers; first transaction succeeds |
| Incorrect reset/powerdown interpretation or sequence | Correct named GPIO/polarity/sequencing in the owning driver | Documented mapping and repeatable chip access after a power cycle |
| Clock absent, incorrect or not enabled | Repair provider/consumer use against firmware settings | Correct supported clock and successful sensor access |
| Access occurs while power is unavailable | Correct runtime-PM sequencing and balanced cleanup | Access only while powered; idle suspend and reopen work |
| I2C controller/address/transport mismatch | Trace and correct only the evidenced transport defect | Correct transaction at the firmware-described address |

Do not mask `-110`, fake successful probe, broadly lengthen delays, or indiscriminately change CSI timing. Use a narrowly scoped board quirk only when the evidence establishes board specificity; document why a generic fix is wrong.

Acceptance: correct chip identity, successful full probe and media registration, no new bus/power errors, and 10 controlled probe/power-cycle trials where the platform permits safe isolation. Exercise failure cleanup too. If cold boot is needed, prepare the test and ask for reboot permission; mark it pending meanwhile. Recheck RGB operation. Commit: `media: fix <established OV7251 defect>`.

If three distinct evidence-backed experiments fail, stop speculative patching. Deliver a blocker report with traces, hypotheses eliminated and the exact next evidence needed (for example a documented Windows resource sequence). This is a blocked bring-up, not working IR.

## Stage 4 — Prove raw monochrome capture

1. Discover the OV7251 entity and its actual supported modes/controls. Select a driver-advertised conservative mode; 640×480 is a candidate only if enumerated. Do not copy RGB dimensions or source IDs.
2. Configure the verified sensor → CSI receiver → supported capture path. Prefer evaluating the existing BE SOC monochrome route; do not assume the Bayer-only BE path supports it. Preserve/restore links and routes used by RGB.
3. Verify the media-bus code, CSI data type, lane count/link rate and receiver timing against this sensor. RGB timing `1155/1269` is not an IR calibration.
4. Audit negotiated fourcc, bytesperline, sizeimage, bytesused, packing, sample alignment and endian order. Existing Y10 entries describe both 10-bit and 16-bit storage paths: establish actual memory layout before conversion. Patch only a demonstrated mismatch.
5. Add `tests/ir-capture-validation.sh` plus a small decoder/analyzer if needed. Discover nodes dynamically; bound waits; save negotiated configuration, sequences, timestamps, payload sizes and image statistics. Handle queue/requeue and teardown correctly. Existing `tests/capture-validation.sh` only understands front/rear RGB.
6. Capture at least 120 consecutive frames and produce viewable grayscale samples. Require coherent scene detail, advancing sequence/timestamps, no empty/truncated payloads, and scene-correlated changes when a target moves or the lens is covered. Different checksums alone can be noise; a static room need not change every frame.

Acceptance: a human-inspected raw IR preview/capture with correct geometry and repeatable exposure/gain response; repeat 10 open/capture/close cycles. Ambient-light capture can pass this stage with the emitter unresolved. Commit capture/format changes with targeted negative tests for empty, truncated and incorrectly packed input.

## Stage 5 — Identify and qualify infrared illumination

1. First determine whether the built-in emitter already follows sensor streaming. Compare matched exposure/gain with controlled ambient lighting and any verified emitter state. A visible camera activity LED is not proof of IR emission.
2. If independent control is needed, trace the actual control path through firmware resources, documented hardware registers or an attributable working driver sequence. Establish controller, polarity, current/duty limits and any synchronization requirement before writing controls. Do not guess undocumented LED power or strobe settings.
3. Implement control in the proper owning subsystem, tied to capture lifetime. Ensure off on stop, failed start, process exit and suspend. Do not add a boot service that leaves illumination permanently on.
4. Prove useful illumination with matched emitter-off/on image comparisons in low light, recognizable scene detail and confirmed shutdown. Measure or otherwise verify the physical emission independently where equipment permits; image brightness alone may reflect automatic exposure.

Acceptance: reproducible, bounded illumination and clean shutdown over 10 cycles. If control cannot be established, report `IR capture works; built-in illumination unverified/blocked` and continue only work independent of it. Commit emitter support separately from sensor/capture fixes.

## Stage 6 — Expose a usable preview without regressing RGB

1. First provide a documented direct capture/preview command that consumes the proven monochrome format correctly. Do not demosaic monochrome Y10 as Bayer.
2. Evaluate the installed libcamera pipeline's monochrome support; record actual capture results. Its existing RGB Simple/SoftISP support does not prove OV7251 support. Add a minimal format/conversion change only if traced and necessary.
3. If a V4L2 compatibility endpoint is needed, add an opt-in IR path to the existing bridge or a small separate process after reviewing resource conflicts. Discover a free endpoint; retain current front/back names and configuration. Use supported grayscale or YUV with neutral chroma and a tested luma conversion. Account for stride and calibrated 10-to-8-bit scaling.
4. Keep IR inactive when unused and handle exclusive hardware ownership cleanly. Do not require simultaneous RGB/IR operation unless it is verified; document any restriction.
5. Open the actual installed preview client and save evidence of live scene changes. Record exact client/package, endpoint, resolution and measured cadence. Native PipeWire/portal qualification is a separate result, not inferred from `cam` or endpoint enumeration.

Acceptance: actual visible IR preview through the documented route, 10 reopen cycles, process-crash recovery, and intact front/rear previews. Commit: `camera: add opt-in OV7251 preview integration`.

## Stage 7 — Lifecycle, packaging and handoff

Run the smallest relevant regression suite after each code change; at final integration run `sh tests/camera-suite.sh --static`, targeted new tests, syntax checks appropriate to each shell, and `git diff --check`. Scripts invoked through `sh` must be POSIX. Build every changed kernel module against the intended kernel. Validate installation/uninstallation in a staging root where possible, including preservation of distribution modules.

With explicit permission, perform three cold starts and five suspend/resume cycles. Also test 10 minutes of sustained capture, 20 open/close cycles, idle power/emitter shutdown and recovery after a killed consumer. Record actual fps, dropped/error frames, CPU usage and relevant logs. Check front and rear in the same named applications used for the baseline after integration. Hardware tests not performed remain `NOT TESTED`; missing IR on the target is a blocking failure, not a passing skip.

Finalize reproducible build/install/rollback instructions, module provenance, confirmed mode/packing, control ranges, known limitations and an evidence table:

| Capability | Required result |
| --- | --- |
| Sensor probe | PASS/FAIL with chip identity and loaded module provenance |
| Raw monochrome image | PASS/FAIL with decoded scene evidence |
| Built-in illumination | PASS/FAIL/BLOCKED with control and shutdown evidence |
| Actual client preview | PASS/FAIL/NOT TESTED with exact client |
| Reopen, idle, crash recovery | Counts and observed failures |
| Cold boot / suspend | Counts or pending permission |
| RGB regression | Separate front/rear results |
| Authentication | NOT IN SCOPE |

Commit final packaging and qualification documentation. Do not push or publish. A working camera is not evidence of Windows Hello-equivalent authentication; any later Howdy/PAM work needs its own enrollment, matching, fallback and security review.

## Final response expected from the executing agent

Report the proven root cause (or unresolved blocker), commits made, files changed, exact commands to reproduce preview and rollback, tests actually completed, remaining approval-dependent tests, and remaining limitations. Include a short next-action prompt if blocked. Never call probe-only success, changing noise, a virtual node, or a camera list a working infrared camera.
