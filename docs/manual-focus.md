# Manual focus: OV8865 and DW9719 control path

Manual focus is a rear-camera lens-actuator path. It is not autofocus: no
focus search, contrast metric, phase detection, or continuous AF loop exists in
this project. A client selects an absolute VCM position and writes the
standard V4L2 focus control.

## Ownership and graph relationship

The rear sensor is the external OV8865 driver. Its ACPI SSDB describes a
`dw9719`-family voice-coil motor (VCM). The IPU4P bridge in this repository
turns that firmware relationship into a software-node property:

```c
/* ipu-bridge.c, after parsing ACPI SSDB */
sensor->vcm_type = ipu_vcm_types[ssdb.vcmtype - 1];

sensor->dev_properties[3] = PROPERTY_ENTRY_REF_ARRAY(
    "lens-focus", sensor->vcm_ref);

/* software-node name is "%s-%u", vcm_type, sensor->link */
/* for example: "dw9719-3" */
```

The supported VCM table maps the firmware type index to `dw9719`:

```c
static const char * const ipu_vcm_types[] = {
    "ad5823", "dw9714", "ad5816", "dw9719", "dw9718",
    "dw9806b", "wv517s", "lc898122xa", "lc898212axb",
};
```

The resulting relationship is:

```text
OV8865 sensor fwnode
    -- lens-focus reference --> software node dw9719-<link>
    -- IPU bridge workqueue --> ACPI/fwnode-backed I2C client
    -- external dw9719 driver --> V4L2 lens sub-device
    -- sensor/media graph --> rear camera pipeline
```

No I2C address is hard-coded here. `i2c_acpi_new_device_by_fwnode()` obtains
the bus/address from the firmware node. The bridge also creates a runtime-PM
device link from the VCM to the sensor.

## Instantiation sequence

`ipu_bridge_instantiate_vcm()` runs when the IPU media device completes sensor
registration:

1. Read the sensor's `lens-focus` fwnode reference.
2. If an I2C client already exists for that fwnode, keep it and return.
3. Otherwise queue work on `system_long_wq`.
4. The worker obtains a runtime-PM reference to the sensor before creating the
   client; this prevents the client from probing against an unpowered lens
   supply.
5. Create the I2C client with board-info type `dw9719` (the `-<link>` suffix
   is stripped from the type used for driver matching).
6. Add `DL_FLAG_PM_RUNTIME` from the VCM client to the sensor.
7. Release the temporary sensor PM reference; the VCM driver owns its normal
   PM lifecycle.

The exact source is `linux-6.19.8/drivers/media/pci/intel/ipu-bridge.c`,
functions `ipu_bridge_create_fwnode_properties()` and
`ipu_bridge_instantiate_vcm()`.

## V4L2 control to actuator register

The external upstream `dw9719.c` driver exposes one standard control:

```c
v4l2_ctrl_new_std(&handler, &dw9719_ctrl_ops,
                  V4L2_CID_FOCUS_ABSOLUTE,
                  0, 1023, 1, 0);
```

The control callback is direct:

```c
case V4L2_CID_FOCUS_ABSOLUTE:
    ret = cci_write(dw9719->regmap,
                    DW9719_VCM_CURRENT, ctrl->val, NULL);
    break;
```

For the DW9719 model, `DW9719_VCM_CURRENT` is register `0x03`, a 16-bit CCI
value. The logical register transaction for a requested position `P` is:

```text
register address width = 8 bits
value width             = 16 bits, big-endian

P = 512 (0x0200):  I2C register/value payload [ 0x03, 0x02, 0x00 ]
P = 1023 (0x03ff): I2C register/value payload [ 0x03, 0x03, 0xff ]
```

The actual 7-bit I2C address is firmware-provided. The byte sequences above
are the payload after the I2C client has selected the slave. The CCI helper
uses `CCI_REG16(3)` and emits a big-endian 16-bit value; it is not the OV5693
sensor's focus register and is not a private IPU4P ioctl.

When the actuator is runtime-powered, a `focus_absolute` write takes a
runtime-PM reference, writes register `0x03`, and releases the reference. If
the actuator is not powered, the upstream callback does not issue a register
write; normal sub-device open/close and runtime PM establish the powered
window.

The DW9719 driver initializes the actuator model before exposing the control.
For the DW9719 model its setup writes the following logical values:

```text
reg 0x02 = 0x02   DW9719_ENABLE_RINGING
reg 0x06 = 0x40   SAC mode 4 at bits [6:4]
reg 0x07 = 0x60   default VCM frequency/prescale
```

Power-down writes `reg 0x02 = 0x01` (`DW9719_SHUTDOWN`) before disabling the
VCM regulator. The driver also ramps the position in 16-step increments during
suspend/resume to avoid an abrupt lens movement.

## Using and verifying the control

Discover the actuator by entity name; do not assume a sub-device minor:

```sh
MEDIA_DEVICE=/dev/mediaX
media-ctl -d "$MEDIA_DEVICE" -p | grep -i -A5 -B2 'dw9719\|lens\|ov8865'

# Resolve the entity's device node from the graph, then inspect controls.
v4l2-ctl -d /dev/v4l-subdevX --list-ctrls
v4l2-ctl -d /dev/v4l-subdevX --get-ctrl=focus_absolute
v4l2-ctl -d /dev/v4l-subdevX --set-ctrl=focus_absolute=512
```

For a camera application that owns the libcamera pipeline, use its control
API or the application's focus UI only if it forwards
`V4L2_CID_FOCUS_ABSOLUTE` to the actuator sub-device. The named C bridge does
not invent an autofocus policy and does not expose a second focus protocol; it
passes the physical camera through libcamera and publishes the selected frame.

## Binding and failure checks

The focus chain is complete only when the external `dw9719` driver has bound.
Check all of these:

```sh
media-ctl -d "$MEDIA_DEVICE" -p
v4l2-ctl -d /dev/v4l-subdevX --list-ctrls
modinfo dw9719
readlink /sys/bus/i2c/devices/*-VCM/driver 2>/dev/null || true
```

The IPU bridge creates an I2C client with modalias `i2c:dw9719`. Some v6.19
kernel trees carry only the `of_device_id` table in `dw9719.c`, with no I2C ID
table, so a client created from ACPI board-info can exist without a bound
driver. In that state there is no working `focus_absolute` write and the
camera graph may remain incomplete. The target kernel must carry a matching
`i2c_device_id` entry or equivalent fwnode match before manual focus can be
claimed.

This repository does not ship the external `dw9719.c` implementation or its
module. It owns the fwnode relationship and I2C-client instantiation; the
external kernel supplies the control range, register driver, regulator, and
runtime-PM behavior described above. Verify the loaded module's source and
vermagic before relying on the exact register table.

## Scope boundary

Manual focus is independent from the OV5693 front camera and the OV7251 IR
camera. It does not change CSI lanes, RAW10 capture, exposure, or the shared
RGB backend. A successful focus-control write proves only actuator control;
focus quality still requires a scene-level test with the rear camera and a
known position target.

Related source and evidence:

- [`linux-6.19.8/drivers/media/pci/intel/ipu-bridge.c`](../linux-6.19.8/drivers/media/pci/intel/ipu-bridge.c)
- [`docs/native-webcam-driver-spec.md`](native-webcam-driver-spec.md)
- [upstream v6.19 `dw9719.c`](https://raw.githubusercontent.com/torvalds/linux/v6.19/drivers/media/i2c/dw9719.c)
