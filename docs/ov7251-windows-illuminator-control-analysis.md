# OV7251 Windows illuminator-control analysis

Date: 2026-09-20. This is read-only static analysis of the extracted Surface
Pro 7 Windows driver. It does not claim that the SP7 board has an electrically
connected emitter, and it does not define a Linux write sequence.

## Inputs and address convention

Inspected binary:

```text
/home/george/repos/sp7-camera/work/microsoft-sp7/extracted/SurfaceUpdate/ov7251/ov7251.sys
PE32+ Windows x64
SHA-256: 201d52f2a1d9441ca20b0a7801ec450276e9161a9aa5617fcc165941c74f0d39
image base: 0x140000000
```

All `0x140...` values below are image virtual addresses. File offsets are raw
on-disk offsets and are stated separately where useful. The reproducible
inspection helper is
[`scripts/ir/analyze-ov7251-windows-control.sh`](../scripts/ir/analyze-ov7251-windows-control.sh).

The exact attributable sensor specification used here is *OV7750/OV7251
CMOS VGA Product Specification*, version 2.12, dated 2015-10-12, sensor
revision 1F. Its register tables are in printed sections 2.4, 4.6, 4.7 and
7.9; the available copy is [the version-2.12 PDF](https://dlscorp.com/wp-content/uploads/2019/02/ov7750_ov7251_csp3_rev1f_ds_2.12.pdf).

## Compact call graph

```text
initialization
  +0x1498 -> 0x140002600
                 -> 0x140007c78
                    -> 0x140004000(table 0x14001b580)
                       reset, 0x0103=0x01
                    -> delay(5)
                    -> 0x140004000(table 0x14001b540)
                       stream off, 0x0100=0x00
                    -> 0x140004000(selected mode table)
                    -> timing/exposure setup

stream callbacks
  +0x14b8 -> 0x140002a20 -> 0x140008e2c(selector=1)
                              -> table 0x14001b560, 0x0100=0x01
  +0x14c0 -> 0x140002c80 -> 0x140008e2c(selector=0)
                              -> table 0x14001b540, 0x0100=0x00

parameter callbacks
  +0x14a0 -> 0x1400086d0  exposure/frame/gain and 0x3b8c..0x3b8d
  +0x14a8 -> 0x140008160  reads exposure/gain/control state
  +0x14b0 -> 0x140008550  separate parameter callback
  +0x1550 -> 0x1400089a0  selector 0x23/0x27 dispatch;
                           selector 0x27 writes 0x3b81
```

The generalized sensor access path is attributable rather than guessed:

```text
table interpreter 0x140004000
  -> bulk write helper 0x14000a6b0
register write helper 0x140003e6c
  -> 0x14000b4b0 -> 0x14000b074
register read helper 0x140003cc4
  -> 0x14000af0c -> 0x14000a8cc
```

## Register semantics from specification 2.12

The specification states that register enable bits use `1 = enable` and
`0 = disable`. It also states that the STROBE pin is high-impedance by default
after reset/standby (printed section 1.1, PDF page 13).

| Address | Specification meaning | Timing/polarity information | Windows evidence |
| --- | --- | --- | --- |
| `0x3005[3]` | STROBE I/O direction | `0=input`, `1=output` | Not separately proven in the Windows control callback |
| `0x3027[3]` | STROBE source select | `0=normal data path`, `1=register-controlled` | Not separately proven in the Windows control callback |
| `0x3009[3]` | Manual STROBE output value | Valid when `0x3027[3]=1` | No direct write identified in the candidate callbacks |
| `0x3b80[5]` | PWM-frame trigger always-on option | `0=SOF/VSYNC trigger only`, `1=always on` | Mode tables initialize this family; exact values are table-specific |
| `0x3b80[4]` | SOF/VSYNC trigger enable | `1=enable` | Mode-table field only; lifecycle use is not isolated |
| `0x3b80[3]` | PWM-frame trigger selection | `0=SOF`, `1=VSYNC` | Mode-table field only |
| `0x3b80[2]` | STROBE-frame trigger selection | `0=VSYNC`, `1=SOF` | Mode-table field only |
| `0x3b80[1:0]` | PWM frame/free polarity fields | The specification names the fields but does not define active-high/active-low mapping | No separate Windows write identified |
| `0x3b81` | Eight-frame STROBE pattern | Each bit selects off/on for one of eight sequential frames; `0=off`, `1=on` | Selector `0x27` writes one byte from the callback value |
| `0x3b82..0x3b83` | PWM frequency divisor, 16-bit | Pad clock divided by `1..65535`; pad clock is `6..27 MHz` | Mode tables contain values; no dynamic write tied to illuminator was proven |
| `0x3b84..0x3b85` | PWM duty-cycle numerator, 16-bit | Duty is `0..100%`: numerator/divisor × 100 | Mode tables contain values; no safe emitter-current meaning follows |
| `0x3b86..0x3b87` | PWM low limit, 16-bit | Units/relationship to the external load are not specified here | Mode-table values only |
| `0x3b88` | STROBE shift sign plus high shift bits | Bit 7 selects positive/negative delay; bits 6:0 are shift bits 30:24 | Mode tables initialize the shift |
| `0x3b89..0x3b8b` | Remaining STROBE frame shift | `strobe_frame_shift[23:0]`; shift/span steps are in system-clock domain | `0x3b8b` is present in mode tables |
| `0x3b8c..0x3b8f` | STROBE frame span, 32-bit | Pulse width after the integration reference; units are system-clock-domain steps | `0x1400086d0` writes `0x3b8c..0x3b8d` dynamically and clamps the value |
| `0x3b90..0x3b91` | STROBE row start, 16-bit | No physical-current meaning is specified | Mode-table values only |
| `0x3b92..0x3b93` | STROBE column start, 16-bit | No physical-current meaning is specified | Mode-table values only |
| `0x3b94..0x3b95` | Manual one-row step, 16-bit | No external-driver meaning is specified | Mode-table values only |
| `0x3b96[7]` | STROBE frame PWM enable | Field name only; exact operational interaction needs runtime/spec context | Mode-table field only |
| `0x3b96[6]` | STROBE frame PWM start | Field name only; exact operational interaction needs runtime/spec context | Mode-table field only |
| `0x3b96[5]` | STROBE polarity | Field name is documented; active-high/active-low mapping is not documented in this revision | No proven Windows polarity write |
| `0x3b96[4]` | STROBE step-pixel option | Field name only | Mode-table field only |
| `0x3b96[3]` | Manual one-row precision option | Field name only | Mode-table field only |
| `0x3b96[2:0]` | STROBE start option | Field name only | Mode-table field only |
| `0x3b97` | Debug control | No production meaning established | No relevant write identified |

Specification page map for the table above: `0x3005`, `0x3027`, and `0x3009`
are described in PDF page 21 (printed section 2.4/table 2-3) and the system
register table on PDF page 71. PWM divisor/duty registers `0x3b82..0x3b85`
are on PDF page 51 (printed section 4.6/table 4-8) and PDF page 88 (printed
section 7.9/table 7-9). Strobe registers `0x3b80..0x3b81` are on PDF page 87;
`0x3b88..0x3b97` are on PDF page 52 (printed section 4.7/table 4-9) and PDF
page 88 (printed section 7.9/table 7-9). The PDF is [OV7750/OV7251 Product
Specification version 2.12](https://dlscorp.com/wp-content/uploads/2019/02/ov7750_ov7251_csp3_rev1f_ds_2.12.pdf).

The specification describes STROBE as a pulse referenced to the beginning of
pixel-array integration, delayed by `strobe_frame_shift`, with width
`strobe_frame_span`. It does not define a safe IR-LED current, external
transistor, current limiter, or board connection. Sensor logic timing must not
be converted into an LED-current setting.

## Windows control-dispatch evidence

### Exposure/timing callback `0x1400086d0`

The initialization block stores this address at object offset `+0x14a0`. The
executable sequence is:

1. Read a 16-bit value from `[RBP-0x2e]` into `R14D`.
2. Derive and write frame length at `0x380e` (the write helper is called with
   two bytes).
3. Write exposure at `0x3500` (three bytes, after a four-bit shift).
4. Write gain at `0x350a` (two bytes).
5. Build a bulk write beginning at `0x3400`.
6. Clamp the same `R14D` value:

```text
if r14d < 0x34:
    r14d = 0x34
if r14d > 0x308:
    r14d = 0x308

write_be16(0x3b8c, r14d)
```

The output bulk write is six bytes: register address `0x3b8c` followed by the
encoded value. The assembly proves the clamp and the write. It does not by
itself prove whether `[RBP-0x2e]` is exposure, line timing, or a framework-
derived strobe parameter; its producer is not resolved by this static slice.
The simultaneous writes to frame length, exposure, and gain make an exposure/
timing relationship plausible, but that remains an inference until the input
structure or a runtime argument is identified.

### Property/control callback `0x1400089a0`

The initialization block stores this address at object offset `+0x1550`. The
callback receives a selector in `EDX` and preserves `R8` in `R15`. The proven
branches are:

```text
if selector == 0x27:
    status = call_indirect(context + 0xff0, 0, 0, 0)
    if status == 0:
        write_register(0x3b81, one_byte(value=R15D))

if selector == 0x23:
    write_register(0x5e00, 0x8c if R15 != 0 else 0x0b)
```

The callback returns an error from the register write and performs a status
check before the `0x3b81` write. No direct executable caller of the function
was found; it is exposed through the parent camera framework's callback table.
The selector/value ABI, defaults, validation range, and relationship to the
strings `Strobe`, `Torch`, and `Flash` remain unresolved.

### Mode tables

The selected-mode path invokes the table interpreter after reset and stream-off
tables. Each Windows mode table writes the `0x3b80..0x3b96` family. Examples
from the four table starts are:

| Table VA | File offset | Selected strobe-family values |
| --- | ---: | --- |
| `0x14001c730` | `0x1b530` | `3b81=a5`, `3b82=10`, `3b84=08`, `3b86=01`, `3b8b=05`, `3b8f=1a`, `3b94/95/96=05/f2/40` |
| `0x14001cff0` | `0x1bdf0` | `3b81=aa`, `3b8b=00`, `3b8e=03`, `3b8f=08`, `3b96=c0` |
| `0x14001d8b0` | `0x1c6b0` | Same strobe-family pattern as the first table |
| `0x14001e170` | `0x1cf70` | Same strobe-family pattern as the first table |

These writes establish sensor initialization state, not emitter activation.
No static evidence shows that ordinary stream-on changes `0x3b81` from the
mode-table value or requests a separate LED.

## GPIO/resource and metadata boundary

The binary contains a dynamically indexed resource-name table containing
`Reset`, `Strobe`, `Torch`, `Flash`, `LedRear`, `LedFront`, `Power0`, `Power1`
and `Standby`. The resource parser and GPIO initialization loops process
ACPI/resource-derived indices, but no direct `Strobe`-specific comparison or
call was found. Therefore the names prove a vendor control vocabulary, not an
SP7 pin mapping.

The INF contains:

```text
HKR,,IRFlashLedIntensity,0x10001, 100
```

The string `IRFlashLedIntensity` is not present in the driver binary. This is
installation metadata; static evidence does not show that it reaches either
callback.

## Lifecycle findings

| Phase | Proven executable behavior | Illumination conclusion |
| --- | --- | --- |
| Initialization | Software reset, delay, stream-off, selected mode table, then timing/exposure setup | Strobe-family registers are initialized, but no separate illuminator enable is proven |
| Stream start | Callback path selects table containing `0x0100=0x01` | No separate `0x3b81` or GPIO-Strobe command is statically tied to start |
| Parameter update | `0x1400086d0` writes exposure/gain and dynamically updates `0x3b8c..0x3b8d`; `0x1400089a0` can write `0x3b81` for selector `0x27` | Sensor-side illumination coupling is plausible but command origin is unknown |
| Stream stop | Callback path selects table containing `0x0100=0x00` | No proven explicit strobe disable or external-controller shutdown |
| Failed start | Register helpers return errors and unwind their local callback, but a complete failure path to stream-off/strobe-off is not resolved | Must not assume cleanup is complete |
| Power-down/reset | Specification says STROBE is high-impedance by default after reset/standby | This is not equivalent to a proven controlled shutdown during a failed stream transaction |

## Evidence classification

| Claim | Classification |
| --- | --- |
| The binary has a generalized sensor register write helper at `0x140003e6c` | Proven executable behavior |
| `0x1400089a0`, selector `0x27`, writes one byte to `0x3b81` after a status check | Proven executable behavior |
| `0x1400086d0` clamps a value to `0x34..0x308` and writes `0x3b8c..0x3b8d` after exposure/gain writes | Proven executable behavior |
| `0x3b8c` is strobe span in specification 2.12 | Proven specification meaning |
| The clamped input is exposure or line timing | Hypothesis; input producer is unresolved |
| Selector `0x27` is the IR illuminator command | Hypothesis; selector dispatch is not attributed to a named property |
| `Strobe` resource label is the physical SP7 emitter | Unresolved; no pin mapping or board schematic |
| `IRFlashLedIntensity=100` drives the OV7251 callbacks | Unresolved; the driver does not contain that string |
| Ordinary stream-on enables the illuminator | Unresolved; no separate enable call is proven |
| The sensor STROBE pin drives a high-current IR LED directly | Unsupported and unsafe; no such current path is documented |

## Smallest next experiment

The smallest experiment is an observation-only Windows callback trace around
one normal control lifetime, not a guessed register write:

```text
open -> mode selection -> stream start -> one known camera property request
     -> one exposure/gain update -> stream stop -> power-down
```

Instrument exactly these events and arguments:

1. At the parent framework call sites that invoke object offsets `+0x14a0` and
   `+0x1550`, record the object pointer (`RCX`), selector (`EDX`), and the
   `R8` value/pointer. For `0x1400086d0`, record the first 16 bytes at `RDX`.
2. Record the value of `context+0xff0` immediately before and after the
   `0x1400089a0` status call.
3. Record before/after sensor writes to `0x0100`, `0x3500..0x350a`,
   `0x3b81`, and `0x3b8c..0x3b8f`.
4. Record any resource/GPIO request whose dynamically resolved name is
   `Strobe`, including provider, pin/index, polarity and transition value.

Expected observations:

- Selector `0x27` followed by a `0x3b81` write identifies the sensor-control
  leg, but not yet the external LED connection.
- A matching `Strobe` GPIO request identifies a separate external-control leg.
- A `0x3b8c` update whose input is the exposure or line period establishes the
  timing relationship.
- No callback or GPIO transition during stream start/stop means ordinary
  streaming does not enable the illuminator.

Rollback is to stop tracing and close the normal Windows camera session. This
experiment performs no sensor-register write, GPIO write, ACPI method, module
change, or capture configuration change. It does not attempt to infer LED
current from sensor voltage, PWM duty, or the `0x34..0x308` clamp.
