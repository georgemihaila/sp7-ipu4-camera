#!/usr/bin/env python3
"""Decode relevant OV7251 register tables from the Windows binary and Linux C."""

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path


RELEVANT = (
    {0x3005, 0x3027, 0x3009}
    | set(range(0x380C, 0x3810))
    | set(range(0x3B80, 0x3B98))
)


def parse_windows_table(data: bytes, offset: int) -> dict[int, int]:
    values: dict[int, int] = {}
    pos = offset
    while pos + 16 <= len(data):
        operation, register, value, _aux = struct.unpack_from("<IIII", data, pos)
        if operation == 0xFFFF:
            return values
        if operation == 1 and register in RELEVANT:
            values[register] = value & 0xFF
        pos += 16
    raise ValueError(f"table at file offset {offset:#x} has no 0xffff sentinel")


def parse_linux_arrays(text: str) -> dict[str, dict[int, int]]:
    arrays: dict[str, dict[int, int]] = {}
    pattern = re.compile(
        r"static const struct reg_value ov7251_setting_vga_(\d+)fps\[\]"
        r"\s*=\s*\{(.*?)\n\};",
        re.S,
    )
    entry = re.compile(r"\{\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)")
    for fps, body in pattern.findall(text):
        arrays[f"linux-{fps}fps"] = {
            int(register, 16): int(value, 16) & 0xFF
            for register, value in entry.findall(body)
            if int(register, 16) in RELEVANT
        }
    if not arrays:
        raise ValueError("no ov7251_setting_vga_*fps arrays found")
    return arrays


def fmt(value: int | None) -> str:
    return "--" if value is None else f"0x{value:02x}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("windows_binary", type=Path)
    parser.add_argument("linux_source", type=Path)
    args = parser.parse_args()

    windows = args.windows_binary.read_bytes()
    linux = parse_linux_arrays(args.linux_source.read_text())
    windows_tables = {
        "windows-14001c730": (0x14001C730, 0x1B530),
        "windows-14001cff0": (0x14001CFF0, 0x1BDF0),
    }
    decoded = {
        name: parse_windows_table(windows, offset)
        for name, (_va, offset) in windows_tables.items()
    }

    print("register Windows-14001c730 Windows-14001cff0 " + " ".join(linux))
    for register in sorted(RELEVANT):
        row = [
            f"0x{register:04x}",
            fmt(decoded["windows-14001c730"].get(register)),
            fmt(decoded["windows-14001cff0"].get(register)),
        ]
        row.extend(fmt(values.get(register)) for values in linux.values())
        print(" ".join(row))

    print("\nwindows table metadata")
    for name, (va, offset) in windows_tables.items():
        print(f"{name} va=0x{va:x} file=0x{offset:x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
