# Paired source-6 startup timelines

All kernel and `ir_diag` times below use the monotonic clock. The
`ir_diag event=stream_start` timestamp is captured at the beginning of the
userspace start operation, before the QBUF/STREAMON calls; its `result` is
known only when that operation returns. It is therefore not a completion
timestamp. The `startup_compare` result lines themselves have no monotonic
timestamp.

## Attempt summary and buffer reconciliation

| Attempt | Userspace result | Source-6 diagnostic | Firmware submission | Queue/in-flight accounting |
| --- | --- | --- | --- | --- |
| 1 | success, decoded frame | retry 0, clean 1 | parked `p0:0..6` -> refed `v0:0..6`; `submit=7 fail=0 fw=0` | incoming `7->0`, active `0->7`; summary active 7 |
| 2 | success, decoded frame | retry 1, clean 1 | parked `p0:0..7` -> refed `v0:0..7`; `submit=8 fail=0 fw=0`; final refeed had no new buffers | incoming `8->0`, active `0->8`, then `7->7`; summary active 7 |
| 3 | success, decoded frame | retry 0, clean 1 | no parked/refed buffers; `submit=0 fail=0 fw=0` | incoming `0->0`, active `7->7` |
| 4 | failed start, `ETIMEDOUT` | retries 1--30, clean 0 | no parked/refed buffers during retries; `submit=0 fail=0 fw=0` | incoming `0->0`, active `8->8`; rollback `active=8->0` |

The active count includes buffers already submitted to firmware and not yet
returned to userspace. A stable active count during a retry is therefore not
evidence of stranded buffers. The failed-attempt rollback is the decisive
ownership evidence available in this run.

## Detailed pair: successful attempt 3 vs failed attempt 4

| Monotonic seconds | Relative to pipeline begin | Attempt 3 success | Attempt 4 failure |
| ---: | ---: | --- | --- |
| 26356.095342597 / 26356.659139154 | userspace open | `stream_open` | `stream_open` |
| 26356.106835787 / 26356.695119446 | userspace start-call timestamp | `stream_start result=success` | `stream_start result=streamon-failure` |
| 26356.108258 / 26356.696043 | 0 ms | `pipeline stream begin source=6` | `pipeline stream begin source=6` |
| 26356.110905 / 26356.698770 | +2.647 / +2.727 ms | snapshot: `hs=0x100`, `status=0x0`, `receiver_errors=0x4000`, `fatal=0` | snapshot: `hs=0x0`, `status=0x0`, `receiver_errors=0x4000`, `fatal=0` |
| 26356.138119 / 26356.727093 | +29.861 / +31.050 ms | snapshot: `hs=0x100`, receiver errors clear, `last=0x4000` | snapshot: `hs=0x0`, receiver errors clear, `last=0x4000` |
| 26356.324858 | +216.600 ms | first `PIN_DATA_READY error=0` | no corresponding completion by this point |
| 26356.343072 | +234.814 ms | refeed `clean=1`, no parked buffers, active `7->7` | -- |
| 26356.343359 | +235.101 ms | summary `clean=1`, result 0 | -- |
| 26357.341516 | -- | -- | snapshot: `hs=0x101`, `status=0x1`, `receiver_errors=0x8643`, `fatal=0` |
| 26357.402018 | +705.975 ms | -- | retry 1 refeed: `clean=0`, no parked/refed, active `8->8` |
| 26377.445725 | +20.749682 s | -- | summary: retry 30, `clean=0`, active 8, result `-110` |
| 26379.412993 | +22.716950 s | -- | first logged `PIN_DATA_READY error=8`; no clean frame |
| 26379.511523 | +22.815480 s | -- | rollback: active `8->0` |

The first direct register divergence in this adjacent pair is the initial
`hs` readback (`0x100` versus `0x0`) about 2.7 ms after the pipeline begins.
The first completion divergence is stronger operational evidence: attempt 3
receives a clean `PIN_DATA_READY error=0` at +216.6 ms, while attempt 4 has no
completion by the comparable interval and does not log a completion until
after the retry budget, with `error=8`.

## Cross-check: successful attempt 1 vs failed attempt 4

Attempt 1 and attempt 4 initially agree more closely: both first report
`hs=0x0`, `status=0x0`, `receiver_errors=0x4000`, then a clear current error
field with `last_receiver_errors=0x4000`. Attempt 1's first source-6
`PIN_DATA_READY` arrives at `26354.418955`, 61.816 ms after its pipeline
begin, with `error=8`; it later reaches a clean frame. Attempt 4 has no
source-6 `PIN_DATA_READY` by the equivalent 61.816 ms. This makes the absence
of an early completion the earliest observable divergence for this pair, while
also showing that the initial `hs` value alone is not sufficient to predict
success.

## Interpretation boundary

The `hs`, receiver-error, and firmware-response fields are software-visible
readbacks. They do not establish electrical lane behavior or prove which side
of the sensor/receiver boundary generated `error=8`. The source code starts
internal IPU subdevices, starts the firmware stream, starts the external sensor,
submits capture buffers, and only then runs `verify_stream_start()`; the exact
firmware contract for that order and for error `8` remains a maintainer
question.
