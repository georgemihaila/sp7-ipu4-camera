#!/usr/bin/env python3
"""Exercise the OV7251 illuminator failure-path contract with mocked I/O.

This is a bounded userspace model of the lifecycle decisions in the patched
driver.  It does not claim to execute kernel callbacks or prove board-level
optical shutdown.  It verifies the state transitions that are otherwise hard
to exercise without loading the module: RMW failure handling, PM success after
emergency power-down, no I2C after power-off, recovery gating, and removal
idempotence.
"""

from collections import defaultdict
from dataclasses import dataclass, field


EIO = -5
EHOSTDOWN = -112
ETIMEDOUT = -110
EINVAL = -22

OUTPUT_REG = 0x3005
OUTPUT_MASK = 1 << 3
PWM_REG = 0x3B96
PWM_MASK = 1 << 7


class MockSensorIO:
    """Powered-state-aware register I/O with targeted one-shot failures."""

    def __init__(self):
        self.powered = False
        self.registers = {OUTPUT_REG: 0xA0, PWM_REG: 0x40}
        self.reads = []
        self.writes = []
        self.read_failures = defaultdict(list)
        self.write_failures = defaultdict(list)

    def fail_read(self, reg, phase, error=EIO):
        self.read_failures[(reg, phase)].append(error)

    def fail_write(self, reg, error=EIO):
        self.write_failures[reg].append(error)

    def read(self, reg, phase):
        self.reads.append((reg, phase, self.powered))
        if not self.powered:
            raise OSError(EHOSTDOWN)
        failures = self.read_failures[(reg, phase)]
        if failures:
            raise OSError(failures.pop(0))
        return self.registers[reg]

    def write(self, reg, value):
        self.writes.append((reg, value, self.powered))
        if not self.powered:
            raise OSError(EHOSTDOWN)
        failures = self.write_failures[reg]
        if failures:
            raise OSError(failures.pop(0))
        self.registers[reg] = value


def rmw(io, reg, mask, enabled):
    try:
        before = io.read(reg, "before")
        after = before | mask if enabled else before & ~mask
        io.write(reg, after)
        verify = io.read(reg, "readback")
    except OSError as exc:
        return exc.args[0]

    if (verify & mask) != (after & mask):
        return EIO
    if (verify ^ before) & ~mask:
        return EIO
    return 0


@dataclass
class Lifecycle:
    io: MockSensorIO = field(default_factory=MockSensorIO)
    power_on: bool = False
    cleanup_needed: bool = False
    cleanup_error: int = 0
    recovery_required: bool = False
    resource_shutdowns: int = 0
    last_cleanup_error: int = 0
    last_primary_error: int = 0

    def power_on_sensor(self):
        self.io.powered = True
        self.power_on = True

    def enable_experimental(self):
        if not self.power_on:
            return EHOSTDOWN
        if self.cleanup_needed or self.recovery_required:
            return EIO
        self.cleanup_needed = True
        ret = rmw(self.io, PWM_REG, PWM_MASK, True)
        if ret:
            return ret
        return rmw(self.io, OUTPUT_REG, OUTPUT_MASK, True)

    def disable_experimental(self):
        if not self.power_on:
            return EHOSTDOWN
        first = 0
        for reg, mask in ((OUTPUT_REG, OUTPUT_MASK), (PWM_REG, PWM_MASK)):
            ret = rmw(self.io, reg, mask, False)
            if ret and not first:
                first = ret
        if not first:
            self.cleanup_needed = False
        return first

    def record_cleanup_error(self, ret):
        if not ret:
            return
        if not self.cleanup_error:
            self.cleanup_error = ret
        self.recovery_required = True
        self.last_cleanup_error = ret

    def power_off(self):
        if not self.power_on:
            return 0
        cleanup_ret = 0
        if self.cleanup_needed:
            cleanup_ret = self.disable_experimental()
            self.record_cleanup_error(cleanup_ret)
        # The real callback now completes emergency resource shutdown and
        # reports PM success; cleanup_ret remains diagnostic state only.
        self.power_on = False
        self.io.powered = False
        self.resource_shutdowns += 1
        return 0

    def stop(self, primary_result=0):
        cleanup_ret = self.disable_experimental() if self.cleanup_needed else 0
        self.record_cleanup_error(cleanup_ret)
        self.last_primary_error = primary_result
        return primary_result if primary_result else cleanup_ret

    def failed_start(self, primary_result):
        cleanup_ret = self.disable_experimental() if self.cleanup_needed else 0
        self.record_cleanup_error(cleanup_ret)
        self.last_primary_error = primary_result
        return primary_result

    def remove(self, runtime_suspended=False):
        if runtime_suspended:
            # The real remove path skips I2C if PM says the sensor is already
            # suspended, even if software state is unexpectedly stale.
            self.power_on = False
            self.io.powered = False
            return 0
        return self.power_off()


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def exercise_success_and_default_off():
    state = Lifecycle()
    state.power_on_sensor()
    initial = dict(state.io.registers)
    before = len(state.io.reads) + len(state.io.writes)
    # Option-off streaming does not call either experimental helper.
    assert_true(state.power_off() == 0, "default-off power-down failed")
    assert_true(len(state.io.reads) + len(state.io.writes) == before,
                "default-off path performed experimental I2C")
    assert_true(state.io.registers == initial,
                "default-off path changed experimental registers")

    state = Lifecycle()
    state.power_on_sensor()
    initial = dict(state.io.registers)
    assert_true(state.enable_experimental() == 0, "successful enable failed")
    assert_true(state.io.registers[OUTPUT_REG] == initial[OUTPUT_REG] | OUTPUT_MASK,
                "output RMW did not preserve unmasked bits")
    assert_true(state.io.registers[PWM_REG] == initial[PWM_REG] | PWM_MASK,
                "PWM RMW did not preserve unmasked bits")
    assert_true(state.stop() == 0, "successful stop failed")
    assert_true(state.io.registers == initial,
                "successful cleanup did not restore unmasked register state")
    assert_true(not state.cleanup_needed, "successful cleanup remained pending")


def exercise_failure(failure_kind, failed_reg):
    state = Lifecycle()
    state.power_on_sensor()
    assert_true(state.enable_experimental() == 0, "setup enable failed")

    if failure_kind == "read":
        state.io.fail_read(failed_reg, "before")
    elif failure_kind == "write":
        state.io.fail_write(failed_reg)
    elif failure_kind == "readback":
        state.io.fail_read(failed_reg, "readback")
    else:
        raise AssertionError(failure_kind)

    assert_true(state.power_off() == 0,
                f"{failure_kind}: PM power-off did not report success")
    assert_true(not state.power_on and not state.io.powered,
                f"{failure_kind}: emergency power-down did not complete")
    assert_true(state.cleanup_error == EIO and state.recovery_required,
                f"{failure_kind}: cleanup fault was not retained")
    shutdowns = state.resource_shutdowns
    io_count = len(state.io.reads) + len(state.io.writes)

    # A later callback/removal cannot issue cleanup I2C or shut down resources
    # twice after the completed power transition.
    assert_true(state.power_off() == 0, f"{failure_kind}: repeat power-off failed")
    assert_true(state.resource_shutdowns == shutdowns,
                f"{failure_kind}: duplicate resource shutdown")
    assert_true(len(state.io.reads) + len(state.io.writes) == io_count,
                f"{failure_kind}: I2C access occurred while unpowered")

    state.power_on_sensor()
    assert_true(state.enable_experimental() == EIO,
                f"{failure_kind}: unresolved cleanup did not block reopen")
    assert_true(state.remove(runtime_suspended=True) == 0,
                f"{failure_kind}: removal recovery failed")
    assert_true(len(state.io.reads) + len(state.io.writes) == io_count,
                f"{failure_kind}: suspended removal performed I2C")


def exercise_error_preservation():
    state = Lifecycle()
    state.power_on_sensor()
    assert_true(state.enable_experimental() == 0, "stop setup enable failed")
    state.io.fail_read(OUTPUT_REG, "before")
    assert_true(state.stop(ETIMEDOUT) == ETIMEDOUT,
                "normal stop overwrote its primary error")
    assert_true(state.last_primary_error == ETIMEDOUT and
                state.cleanup_error == EIO,
                "normal stop did not retain both errors")

    state = Lifecycle()
    state.power_on_sensor()
    assert_true(state.enable_experimental() == 0, "start setup enable failed")
    state.io.fail_write(OUTPUT_REG)
    assert_true(state.failed_start(EINVAL) == EINVAL,
                "failed start overwrote its primary error")
    assert_true(state.last_primary_error == EINVAL and
                state.cleanup_error == EIO,
                "failed start did not retain both errors")


def exercise_remove_after_active_reopen():
    state = Lifecycle()
    state.power_on_sensor()
    assert_true(state.enable_experimental() == 0, "remove setup enable failed")
    state.io.fail_read(OUTPUT_REG, "readback")
    assert_true(state.power_off() == 0, "remove setup power-off failed")
    state.power_on_sensor()
    assert_true(state.remove(runtime_suspended=False) == 0,
                "active removal after reopen failed")
    assert_true(state.resource_shutdowns == 2,
                "active removal did not perform its one required shutdown")
    assert_true(not state.io.powered, "active removal left sensor powered")


def main():
    exercise_success_and_default_off()
    for failed_reg in (OUTPUT_REG, PWM_REG):
        for kind in ("read", "write", "readback"):
            exercise_failure(kind, failed_reg)
    exercise_error_preservation()
    exercise_remove_after_active_reopen()
    print("PASS: mocked OV7251 cleanup read/write/readback, PM recovery, reopen, and removal paths")


if __name__ == "__main__":
    main()
