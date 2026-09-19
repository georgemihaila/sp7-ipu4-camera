# Questions for IPU4/OV7251 maintainers

These questions are limited to the captured source-6 behavior. They are not
requests to change PHY values or fatal handling.

## Source-6 initialization and receiver state

1. For an IPU4P source-6 stream with OV7251 on CSI-2 1, one lane, VC 0, and
   capture pin 0, what are the expected initial and post-lock values of the
   receiver `hs` field? Is `hs=0x0` at the first snapshot normal, and what do
   `0x100` and `0x101` specifically represent?
2. Is `receiver_errors=0x4000` during the first two startup snapshots an
   expected initialization transition? Which receiver-error bits correspond to
   the later `0x400`, `0x8400`, and `0x8643` values?
3. Does source 6 require an explicit receiver or firmware reset between
   stream attempts, or should `STREAMOFF` plus the existing pipeline teardown
   fully reset the relevant state?

## Sensor/receiver/start ordering

4. The recorded driver order is: start internal IPU subdevices, call
   `start_stream_firmware()`, call the external sensor's `s_stream(1)`, submit
   capture buffers, then run `verify_stream_start()`. Is this the required
   order for OV7251 source 6, or should the sensor LP-to-HS transition occur
   after a different receiver or buffer-arming boundary?
5. Is the existing sensor-bounce sequence (`s_stream(0)`, drain/read receiver
   errors, delay, `s_stream(1)`, refeed parked buffers) valid for OV7251, or was
   it only established for the marginal OV5693 path described in the source
   comment?
6. During a failed start, is it expected that eight buffers remain in the
   firmware active queue while `verify_stream_start()` retries? Should a
   refeed with `active=8`, `incoming=0`, and no parked buffers be a no-op?

## Firmware response and buffer semantics

7. What is the authoritative firmware definition of `PIN_DATA_READY error=8`?
   Is it a per-buffer recoverable result, a receiver framing/synchronization
   error, or a terminal stream condition? Is the value stable across the
   firmware versions used by IPU4P?
8. When `PIN_DATA_READY` reports `error=8`, does the firmware guarantee that
   the corresponding buffer has completed ownership transfer and may be
   requeued without a stream restart? Are the packet records' repeated lines
   expected for one buffer completion?
9. In the source-6 refeed diagnostic, does `fw=0` mean that every firmware
   submission was accepted, and is `fail=0` independent of the later
   `PIN_DATA_READY error` value?
10. Does `-110` from `verify_stream_start()` after 30 retries have a firmware
    status counterpart, or is it purely the Linux driver's clean-frame timeout?
11. Are `fatal_receiver_errors` intentionally retained across stream
    boundaries? What operation is the supported way to clear or acknowledge
    that field, and how should a maintainer distinguish a retained snapshot
    from a newly latched error?
12. After `verify_stream_start()` reports `-ETIMEDOUT`, can source 6 still emit
    receiver SOF/EOF and `PIN_DATA_READY error=8` before stream stop and
    `flush_firmware_streamon_fail()` complete? Is that late response expected
    teardown behavior, and what ordering guarantees distinguish it from the
    response that caused the startup failure?

The run cannot answer these questions from changing payload hashes alone. It
also cannot distinguish sensor output failure from receiver synchronization
failure without an attributable firmware contract or synchronized physical
measurement.
