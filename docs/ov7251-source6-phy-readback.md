# OV7251 source-6 PHY readback

Date: 2026-09-18
Kernel: `6.19.8-3.surface.fc43.x86_64`

## Test method

The existing source-6 platform diagnostic was extended to read the IPU4P
buttress CPHY DLL override, DPHY DLL override, and BB AFE registers immediately
before source-6 receiver timing. The candidate module was built against the
running kernel and hot-loaded after a normal, non-forced unload of
`intel_ipu4p_isys`; no reboot and no device-register writes were used by the
diagnostic.

Candidate module SHA-256:

```text
7e7c1bd355966a954aaf0411915cef3bf14782872dc97d345599022741ead837
```

The bridge was stopped only for the bounded capture and was restarted after
the candidate was unloaded. The installed baseline module was restored and
verified as:

```text
10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce
```

## Demonstrated readback

Immediately before the source-6 receiver timing sequence, Linux reported:

```text
source-6 before timing platform:
legacy hpll=0x0 isclk=0x0 override=0x0 port=0x3895
combo hpll=0x0 isclk=0x0 override=0x0 port=0x3895
bscan=0x8040200

source-6 before timing PHY readback:
bb4=(0x1001b,0x41,0x4000000f)
bb6=(0x1001b,0x41,0x40000015)
bb10=(0x1001b,0x41,0x40000015)
bb12=(0x1001b,0x41,0x4000000f)
bb14=(0x1001b,0x41,0x40000015)
```

Each tuple is `(CPHY_DLL_OVRD, DPHY_DLL_OVRD, BB_AFE_CONFIG)`. These values
match the current Linux global BB setup and establish what the hardware
actually returned during the IR start. They do not establish which building
block Windows selects for source 6 or that these values are correct for the
OV7251.

## Capture result

The route was the known IR path:

```text
ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> Intel IPU4 CSI2 BE SOC -> /dev/video42
```

The capture requested `Y10 ` at 640×480. It timed out with zero bytes. The
receiver enabled one lane with timing `0/627/0/647`; the sensor `s_stream(1)`
call succeeded, but all 31 sensor attempts produced no clean frame and the
receiver repeatedly reported status `0x4000`. Stream stop also timed out.

## Interpretation

This is additional demonstrated Linux state, not a fix. It narrows the
unknowns only by confirming the readback of the globally configured BB values.
It does not provide the missing source-6-to-BB mapping or known-good Windows
source-6 HPLL/ISCLK/port/PHY values. No further PHY mutation is justified by
this trace.

Raw capture and complete kernel logs are retained outside Git under
`/var/tmp/ov7251-source6-bbtrace-20260918/`.
