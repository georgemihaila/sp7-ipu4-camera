# Upstream-readiness checklist

This repository is a hardware-specific development snapshot, not a complete
upstream camera stack. Use this status when reviewing or submitting changes.

## Plan for dependable SP7 use

The initial support target is a Surface Pro 7 with the validated firmware and
kernel combination. Broad IPU4P support and a complete camera application stack
remain separate goals.

1. **Close the current reliability defects (in progress).** Make installation
   and removal exact and conflict-safe; make stream-start and request failures
   return clean errors; keep CSI-2 error reporting race-free; and keep sensor
   format, crop, blanking, and exposure state consistent. The static suite must
   exercise these failure cases.
2. **Build and package the exact artifact.** Build against the target kernel
   tree, audit module dependencies and metadata, and verify installation and
   removal in a temporary module tree before touching the SP7.
3. **Qualify that artifact on the SP7.** After a clean boot, run repeated front
   and rear captures, mode/control changes, bounded stream restart and runtime
   power recovery checks, and inspect kernel logs. Record the kernel build,
   module hashes, and results so the tested artifact is identifiable.
4. **Keep the support boundary explicit.** OV8865 still comes from an external
   driver, OV7251 probe fails on the validated unit, and the GUI portal path is
   unverified. Do not claim those paths or other IPU4P systems are supported
   until their own source and hardware checks pass.

Readiness means steps 1–3 pass for the stated SP7 target. Upstreaming, support
for other systems, and the limitations in step 4 need their own work.

## Validation status

| Area | Status | Evidence/qualification |
|---|---|---|
| IPU4P parent/ISYS/PSYS and OV5693 changes | Hardware-validated on one Surface Pro 7 | Fedora 43 linux-surface 6.19.8-3.surface; raw front/rear captures are recorded in `autotest/RESULT.md`. |
| SP7 DMI timing behavior | Hardware-validated on that SP7; static checks elsewhere | The quirk is restricted by DMI, IPU4P PCI ID, source/lane identity, CPD date `0x20191030`, and CSS release `0x20181222`; generic mismatches fail closed. |
| CSI-2 error/queue/PM paths | Compile/static validation plus targeted hardware runs where recorded | `tests/task9-csi2-static.sh`, `tests/task10-production-static.sh`, and the reports under `reports/`. |
| Build metadata and module discovery | Static/mechanical validation; compile depends on external KDIR | `scripts/build-modules.sh` and `docs/external-kernel-integration.md`. |
| libcamera, IPA, PipeWire, portal | Fedora Simple + SoftISP processed capture validated; GUI app path incomplete | Fedora 0.7.1 matches the IPU4P's `intel-ipu6` media identity and unpacked `BG10` processed format, so this repository carries no libcamera patch. Both cameras produce processed frames; rear output is near-black at low initial exposure and the GUI app path still needs validation. See `libcamera/README.md`. |
| OV8865 | External requirement | The sensor was hardware-tested through an externally available driver; no OV8865 driver source is included here. |
| IR OV7251 | Known limitation | I2C probe fails on the validated unit and is ignored. |

## Review checklist

## Module metadata audit

The IPU4P parent is `intel-ipu4p` and advertises PCI alias
`pci:v00008086d00008A19sv*sd*bc*sc*i*`. It declares firmware
`ipu4p_cpd.bin` and the following soft-dependencies:

```text
pre: ipu-bridge intel_ipu4p_isys_csslib intel_ipu4p_psys_csslib
post: intel_ipu4p_isys intel_ipu4p_psys
```

The IPU parent, ISYS, PSYS, CSS libraries, bridge, and in-tree OV5693 driver
carry GPL module metadata. The OV5693 source provides ACPI/OF tables, while
OV8865 remains an external driver requirement. Verify a build-specific result
with `modinfo intel-ipu4p` (or `modinfo` on the generated `.ko`) because
module naming and alias formatting can differ between kernel build systems.

- [ ] Rebase the overlay against the exact target kernel and provide the
  kernel-tree integration changes for IPU Kconfig/Makefiles and OV5693.
- [ ] Confirm all module metadata with `modinfo`, including the PCI alias,
  `MODULE_FIRMWARE`, GPL license, and the parent soft-dependencies.
- [ ] Confirm the CPD redistribution/license status with the firmware owner;
  the Microsoft-signed `ipu4p_cpd.bin` is not distributed here.
- [ ] Review the SP7 DMI exception with upstream maintainers; it is a
  compatibility exception for a specific firmware/CSS pair, not a general
  firmware-version bypass.
- [ ] Add or point to an upstream OV8865 driver and complete libcamera IPA /
  pipeline-handler integration before claiming normal camera applications are
  supported.
- [ ] Repeat hardware validation on additional IPU4P systems and kernel
  versions; current captures do not establish portability.

## Reproducible checks

Run from the repository root:

```sh
./tests/camera-suite.sh
for script in *.sh libcamera/*.sh scripts/*.sh tests/*.sh; do
    sh -n "$script"
done
git diff --check
```

The default suite is hardware-independent. `--live` only inspects currently
available hardware and performs bounded captures; it does not install,
unload/reload, reboot, or modify services.
