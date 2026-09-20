#!/usr/bin/env python3
"""Check the bounded OV7251 illuminator experiment invariants."""

from pathlib import Path
import re
import sys


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


if len(sys.argv) != 2:
    fail(f"usage: {sys.argv[0]} PATCHED_OV7251_C")

source = Path(sys.argv[1]).read_text(encoding="utf-8")

require(
    "static bool experimental_strobe_output;" in source
    and "module_param_named(experimental_strobe_output, experimental_strobe_output,"
    in source
    and "bool, 0444);" in source,
    "experimental option is not a read-only, default-off load-time parameter",
)
require(
    "experimental_strobe_output = true" not in source,
    "experimental option has a source-level enabled default",
)
require(
    "static bool strobe_diagnostics;" in source
    and "module_param_named(strobe_diagnostics, strobe_diagnostics, bool, 0444);"
    in source,
    "bounded diagnostics parameter is missing or writable",
)
for fragment in (
    "bool power_on; /* true while the sensor can accept register I/O */",
    "bool strobe_cleanup_needed;",
    "int strobe_cleanup_error;",
    "bool strobe_recovery_required;",
    "static void ov7251_record_strobe_cleanup_error",
):
    require(fragment in source, f"runtime-PM failure state missing: {fragment}")

update_start = source.index("static int ov7251_update_strobe_bit")
update_end = source.index("static const u16 ov7251_strobe_diag_regs", update_start)
update = source[update_start:update_end]
for fragment in (
    "ov7251_read_reg(ov7251, reg, &before)",
    "after = enable ? before | mask : before & ~mask;",
    "ov7251_write_reg(ov7251, reg, after)",
    "ov7251_read_reg(ov7251, reg, &verify)",
    "(verify ^ before) & (u8)~mask",
):
    require(fragment in update, f"RMW/readback invariant missing: {fragment}")

diag_start = source.index("static const u16 ov7251_strobe_diag_regs")
diag_end = source.index("static void ov7251_trace_strobe_state", diag_start)
diag = source[diag_start:diag_end]
for reg in [
    "0x3005",
    "0x3027",
    "0x3009",
    *[f"0x{reg:04x}" for reg in range(0x3B80, 0x3B97)],
]:
    require(reg in diag, f"diagnostic register missing: {reg}")

stream_start = source.index("static int ov7251_s_stream")
stream_end = source.index("static int ov7251_get_frame_interval", stream_start)
stream = source[stream_start:stream_end]

ctrl_pos = stream.index("__v4l2_ctrl_handler_setup")
enable_pos = stream.index("ov7251_ir_illuminator_on")
stream_on_pos = stream.index("OV7251_SC_MODE_SELECT_STREAMING")
require(ctrl_pos < enable_pos < stream_on_pos, "enable is not after controls and before 0x0100=1")
require(
    "if (experimental_strobe_output)" in stream,
    "enable is not guarded by the default-off option",
)

stop_pos = stream.index("} else {")
stop = stream[stop_pos:]
disable_pos = stop.index("ov7251_ir_illuminator_off")
standby_pos = stop.index("OV7251_SC_MODE_SELECT_SW_STANDBY")
pm_put_pos = stop.index("pm_runtime_put")
require(
    disable_pos < standby_pos < pm_put_pos,
    "normal stop does not disable before standby and runtime-PM release",
)
require(
    "normal-stop" in stop and "shutdown not confirmed" in stop,
    "normal-stop cleanup failure is not reported",
)

error_pos = stream.index("err_power_down:")
error = stream[error_pos:]
require(
    error.index("ov7251_ir_illuminator_off")
    < error.index("pm_runtime_put"),
    "failed-start cleanup is not before runtime-PM release",
)
require(
    "failed-start" in error and "shutdown not confirmed" in error,
    "failed-start cleanup failure is not reported",
)
require(
    "ov7251_record_strobe_cleanup_error(ov7251, \"failed-start\"" in error,
    "failed-start cleanup fault is not retained",
)

power_start = source.index("static int ov7251_set_power_off")
power_end = source.index("static int ov7251_set_hflip", power_start)
power = source[power_start:power_end]
require(
    power.index("ov7251_ir_illuminator_off")
    < power.index("clk_disable_unprepare"),
    "runtime power-off does not retry cleanup before clock/regulator shutdown",
)
require(
    "shutdown not confirmed before power-off" in power,
    "runtime power-off cleanup failure is not reported",
)
require(
    "if (!ov7251->power_on)" in power
    and "sensor already unpowered" in power,
    "runtime power-off lacks duplicate-shutdown guard",
)
require(
    "ov7251_record_strobe_cleanup_error(ov7251," in power,
    "runtime power-off does not retain cleanup failure",
)
require(
    "ov7251->power_on = false;" in power
    and power.index("ov7251->power_on = false;") < power.index("clk_disable_unprepare"),
    "runtime power-off does not mark the sensor unpowered before resource shutdown",
)
require(
    "optical-shutdown=not-independently-verified" in power
    and "/* The sensor power transition succeeded; cleanup faults remain recorded. */"
    in power
    and "return 0;" in power,
    "runtime power-off does not separate PM success from optical verification",
)

remove_start = source.index("static void ov7251_remove")
remove_end = source.index("static const struct dev_pm_ops", remove_start)
remove = source[remove_start:remove_end]
require(
    remove.index("pm_runtime_disable") < remove.index("media_entity_cleanup")
    < remove.index("mutex_destroy"),
    "remove destroys driver state before completing PM shutdown",
)
require(
    "if (ov7251->power_on)" in remove
    and "PM says suspended while power state is on" in remove,
    "remove lacks powered-state and suspended-state protection",
)
require(
    remove.index("pm_runtime_barrier") < remove.index("pm_runtime_disable"),
    "remove does not drain pending runtime-PM work before teardown",
)
require(
    "strobe enable refused pending" in source
    and "strobe_recovery_required" in source,
    "unresolved cleanup does not block experimental reactivation",
)
require(
    "static int ov7251_ir_illuminator_on" in source
    and "static int ov7251_ir_illuminator_off" in source,
    "explicit IR illuminator on/off driver methods are missing",
)

require(
    source.count("ov7251_update_strobe_bit(ov7251") >= 4,
    "candidate does not contain both enable and both disable bit operations",
)
require(
    not re.search(r"0x3027|0x3009", update),
    "candidate helper writes an unrelated diagnostic-only register",
)

print("PASS: OV7251 illuminator option, bounded diagnostics, RMW preservation, and cleanup ordering")
