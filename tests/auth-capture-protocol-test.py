#!/usr/bin/env python3
"""Protocol-level tests for bounded capture, without a camera or PAM."""

import os
from pathlib import Path
import stat
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/ir"))
import sp7_ir_protocol as protocol  # noqa: E402


HELPER_TEMPLATE = r'''#!{python}
import signal
import struct
import sys
import time

mode = {mode!r}
count = int(sys.argv[2])
header = struct.Struct("<8sIIIIIQQ")
magic = b"SP7IRF01"
payload = b"x" * (640 * 480)

if mode == "busy":
    raise SystemExit(2)
if mode == "blocked":
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    time.sleep(10)
    raise SystemExit(0)
if mode == "malformed":
    sys.stdout.buffer.write(header.pack(b"BADMAGIC", 1, 640, 480, len(payload), 1,
                                        time.monotonic_ns() // 1_000_000_000,
                                        (time.monotonic_ns() // 1_000) % 1_000_000))
    sys.stdout.buffer.flush()
    raise SystemExit(0)
if mode == "truncated":
    now = time.monotonic_ns()
    sys.stdout.buffer.write(header.pack(magic, 1, 640, 480, len(payload), 1,
                                        now // 1_000_000_000, (now // 1_000) % 1_000_000))
    sys.stdout.buffer.write(payload[:5])
    sys.stdout.buffer.flush()
    raise SystemExit(0)

for sequence in range(1, count + 1):
    now = time.monotonic_ns()
    seconds = now // 1_000_000_000
    usec = (now // 1_000) % 1_000_000
    if mode == "stale":
        seconds -= 10
    sys.stdout.buffer.write(header.pack(magic, 1, 640, 480, len(payload), sequence,
                                        seconds, usec))
    sys.stdout.buffer.write(payload)
    sys.stdout.buffer.flush()
'''


def write_helper(directory, mode):
    path = directory / "helper.py"
    path.write_text(HELPER_TEMPLATE.format(python=sys.executable, mode=mode), encoding="utf-8")
    path.chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
    protocol.EXPECTED_HELPER_PATH = path
    return path


def expect_error(directory, mode, expected):
    write_helper(directory, mode)
    session = protocol.DirectCaptureSession(1, budget=0.4)
    try:
        session.read_luma()
    except expected:
        return
    finally:
        session.release()
    raise AssertionError(f"{mode} did not raise {expected.__name__}")


def main():
    with tempfile.TemporaryDirectory(prefix="sp7-auth-protocol-") as temporary:
        directory = Path(temporary)
        # The production resolver requires a root-owned installed helper. The
        # test substitutes only that trust-boundary check with its temp helper.
        protocol.resolve_helper = lambda path=None: (
            protocol.EXPECTED_HELPER_PATH if path is None else path
        )
        write_helper(directory, "valid")
        session = protocol.DirectCaptureSession(2)
        first = session.read_luma()
        second = session.read_luma()
        assert first is not None and second is not None
        assert len(first[1]) == protocol.WIDTH * protocol.HEIGHT
        session.release()

        expect_error(directory, "malformed", protocol.Sp7IrCaptureError)
        expect_error(directory, "truncated", protocol.Sp7IrCaptureError)
        expect_error(directory, "stale", protocol.Sp7IrCaptureError)
        expect_error(directory, "busy", protocol.Sp7IrBusy)

        write_helper(directory, "valid")
        session = protocol.DirectCaptureSession(1, deadline=time.monotonic() - 0.1)
        try:
            session.read_luma()
        except protocol.Sp7IrTimeout:
            pass
        else:
            raise AssertionError("expired face-attempt deadline was not enforced")
        finally:
            session.release()

        write_helper(directory, "blocked")
        session = protocol.DirectCaptureSession(1, budget=0.4)
        started = time.monotonic()
        try:
            session.read_luma()
        except protocol.Sp7IrTimeout:
            elapsed = time.monotonic() - started
            assert elapsed < 1.5, elapsed
        else:
            raise AssertionError("blocked helper did not hit the deadline")
        finally:
            session.release()

        write_helper(directory, "blocked")
        session = protocol.DirectCaptureSession(1, budget=3.0)
        session.start()
        process = session.process
        session.release()
        assert process.poll() is not None

    print("auth-capture-protocol-test: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
