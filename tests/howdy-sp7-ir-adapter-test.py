#!/usr/bin/env python3
"""Offline tests for the pinned Howdy sp7_ir recorder contract."""

import importlib.util
from pathlib import Path
import stat
import sys
import tempfile
import time
import types

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
IR = ROOT / "scripts" / "ir"
sys.path.insert(0, str(IR))
import sp7_ir_protocol as protocol  # noqa: E402


HELPER = r'''#!{python}
import struct
import sys
import time

mode = {mode!r}
count = int(sys.argv[2])
header = struct.Struct("<8sIIIIIQQ")
magic = b"SP7IRF01"
payload = bytes([7]) * (640 * 480)
if mode == "busy":
    raise SystemExit(2)
if mode == "error":
    raise SystemExit(1)
for sequence in range(1, count + 1):
    now = time.monotonic_ns()
    sys.stdout.buffer.write(header.pack(
        magic, 1, 640, 480, len(payload), sequence,
        now // 1_000_000_000, (now // 1_000) % 1_000_000))
    sys.stdout.buffer.write(payload)
    sys.stdout.buffer.flush()
'''


def load_reader():
    """Load the adapter with a tiny cv2 substitute; no OpenCV is required."""
    fake_cv2 = types.ModuleType("cv2")
    fake_cv2.CAP_PROP_FRAME_WIDTH = 3
    fake_cv2.CAP_PROP_FRAME_HEIGHT = 4
    fake_cv2.CAP_PROP_FRAME_COUNT = 7
    fake_cv2.COLOR_GRAY2BGR = 8

    def cvt_color(gray, _code):
        return np.repeat(gray[:, :, None], 3, axis=2)

    fake_cv2.cvtColor = cvt_color
    sys.modules["cv2"] = fake_cv2
    package = types.ModuleType("recorders")
    package.__path__ = [str(IR)]
    sys.modules["recorders"] = package
    sys.modules["recorders.sp7_ir_protocol"] = protocol
    name = "recorders.sp7_ir_reader_test"
    spec = importlib.util.spec_from_file_location(name, IR / "sp7_ir_reader.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module.sp7_ir_reader


def write_helper(directory, mode="valid"):
    path = directory / "helper.py"
    path.write_text(HELPER.format(python=sys.executable, mode=mode), encoding="utf-8")
    path.chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
    protocol.EXPECTED_HELPER_PATH = path
    return path


def expect_exception(reader_cls, expected):
    reader_cls.__init__.__globals__["EXPECTED_HELPER_PATH"] = protocol.EXPECTED_HELPER_PATH
    reader = reader_cls(str(protocol.EXPECTED_HELPER_PATH))
    try:
        reader.grab()
        reader.read()
    except expected:
        assert reader._released, "capture error did not release the adapter"
    else:
        raise AssertionError(f"expected {expected.__name__}")
    finally:
        reader.release()


def main():
    reader_cls = load_reader()
    with tempfile.TemporaryDirectory(prefix="sp7-howdy-adapter-") as temporary:
        directory = Path(temporary)
        illuminator = directory / "experimental_strobe_output"
        illuminator.write_text("Y\n", encoding="ascii")
        protocol.EXPECTED_ILLUMINATOR_PARAM = illuminator
        protocol.resolve_helper = lambda path=None: (
            protocol.EXPECTED_HELPER_PATH if path is None else path
        )

        write_helper(directory)
        reader_cls.__init__.__globals__["EXPECTED_HELPER_PATH"] = protocol.EXPECTED_HELPER_PATH
        config = types.SimpleNamespace(
            _sp7_ir_attempt_deadline=time.monotonic() + 5.0
        )
        reader = reader_cls(str(protocol.EXPECTED_HELPER_PATH), config)
        assert reader.grab() is True
        assert reader.isOpened() is True
        assert reader.get(3) == 640.0
        assert reader.get(4) == 480.0
        assert reader.get(999) == 0.0
        assert reader.set(0, 1) is False
        assert reader.open() is False
        for _ in range(protocol.MAX_FRAMES):
            ok, image = reader.read()
            assert ok is True
            assert image.shape == (480, 640, 3)
            assert image.dtype == np.uint8
            assert int(image[0, 0, 0]) == 7
        ok, image = reader.read()
        assert (ok, image) == (False, None)
        assert reader.exhausted is True
        assert reader._session.requested_frames == 12
        assert reader._session._deadline <= config._sp7_ir_attempt_deadline
        assert reader._session._deadline - time.monotonic() <= 3.1
        reader.release()
        assert reader.isOpened() is False
        assert reader.grab() is False

        write_helper(directory, "busy")
        expect_exception(reader_cls, protocol.Sp7IrBusy)
        write_helper(directory, "error")
        expect_exception(reader_cls, protocol.Sp7IrCaptureError)

    print("howdy-sp7-ir-adapter-test: PASS")


if __name__ == "__main__":
    main()
