# Rear OV8865 CSI-2 reliability investigation

Date: 2026-09-15

## Conclusion

No evidence-supported driver fix was identified in this repository. The rear
OV8865 is an external sensor driver, and the live host is running an older
pre-commit IPU4P module. The repository's rear CSI-2 setup is consistent with
the sensor contract: source 3 on CSI-2 port 0, four lanes, 360 MHz link
frequency, 10-bit Bayer input, and 3264x2448 frames.

The FIFO overflow and Frame sync error pair is a real receiver status event,
not a logging artifact: the IPU4P error handler maps receiver bit 3 to FIFO
overflow and bit 7 to Frame sync error, records fatal state for the latter,
and reports it. The event occurs at the beginning of the rear receiver
lifetime, after receiver enable and around sensor start, rather than during
buffer teardown. Existing successful captures show no queue starvation or
teardown failure. The evidence is therefore most consistent with a
start-boundary receiver/OV8865 PHY timing interaction, but it does not isolate
receiver programming from external sensor timing.

## Repository review

- `linux-6.19.8/drivers/media/pci/intel/ipu-isys-csi2.c` derives receiver
  timing from the remote sensor's active format and link frequency, enables
  the receiver before firmware and external sensor streaming, and resets
  error status on a clean final disable.
- `linux-6.19.8/drivers/media/pci/intel/ipu4/ipu4p-isys-csi2.c` uses the
  generic timing path for rear source 3. The special fixed timing adjustment
  is restricted to front source 7 and was not changed.
- `linux-6.19.8/drivers/media/pci/intel/ipu-isys-video.c` queues and starts
  firmware before calling the external sensor's `s_stream(1)`, checks receiver
  errors before and after that call, and preserves fatal error reporting.
- There is no OV8865 source or timing table in this tree. The sensor is
  supplied by the external/kernel module stack.

## Evidence

Five valid tracked rear traces produced complete 47,941,632-byte captures.
The FIFO/Frame-sync pair appears in two of those five traces; the other valid
traces show no fatal pair (one records only informational receiver status
`0x4000`, inter-frame long-packet discard). The successful traces show the
rear stream transition `stream_count=0 -> 1 -> 0`, four lanes, 360 MHz, and
receiver timing `ctermen=0 csettle=672 dtermen=0 dsettle=658`.

Static and build validation passed:

- `./tests/camera-suite.sh --static`: 4/4 static tasks passed.
- `KDIR=/lib/modules/6.19.8-sp7cam-test1/build ./scripts/build-modules.sh -j2`:
  passed.
- `git diff --check`: passed.

A bounded live run attempted ten rear starts. It reached 0/10 usable
captures because the currently loaded pre-commit graph rejected the dynamic
CSI2-BE-SOC route enable with `EINVAL` after graph reset; it did not reach
sensor streaming and produced no new receiver-error result. This is a live
stack/provenance limitation, not evidence of ten rear capture failures.

The installed `intel_ipu4p_isys` module does not contain this tree's current
diagnostic strings or parameters, confirming that live results cannot prove a
candidate change. No module was unloaded, replaced, or rebooted. Front source
7 code and behavior were not changed.

## Remaining blocker

To distinguish rear receiver timing/programming from OV8865 sensor timing and
to validate a fix, the exact rebuilt IPU4P module and the relevant external
OV8865 module must be loaded in a controlled test environment. That requires
an external live-module/provenance change outside this repository. Accordingly,
no driver change is made here and receiver errors remain fully reported.
