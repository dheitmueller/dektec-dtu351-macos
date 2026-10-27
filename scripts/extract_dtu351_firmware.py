#!/usr/bin/env python3
"""Extract the DTU-351 FX3 and FPGA payloads from DekTec's BSD driver source."""

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path


ARRAY_RE = r"const\s+UInt8\s+{name}\[\]\s*=\s*\{{(?P<body>.*?)\}};"
FX3_RECORDS = (
    ("FX3_MICROCODE_DTU351_0", 0x00000100),
    ("FX3_MICROCODE_DTU351_1", 0x40003100),
    ("FX3_MICROCODE_DTU351_2", 0x40013100),
    ("FX3_MICROCODE_DTU351_3", 0x40023400),
)
FX3_ENTRY = 0x4000FD10


def array(source: str, name: str) -> bytes:
    match = re.search(ARRAY_RE.format(name=re.escape(name)), source, re.DOTALL)
    if not match:
        raise ValueError(f"array {name} not found")
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", match.group("body"), flags=re.DOTALL)
    values = [int(token, 16) for token in re.findall(r"0x([0-9a-fA-F]{1,2})\b", body)]
    return bytes(values)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    args.output.mkdir(parents=True, exist_ok=True)

    records = [(address, array(source, name)) for name, address in FX3_RECORDS]
    with (args.output / "dtu351-fx3.bin").open("wb") as output:
        output.write(b"DTU351F1")
        output.write(struct.pack("<I", len(records) + 1))
        for address, payload in records:
            output.write(struct.pack("<II", address, len(payload)))
            output.write(payload)
        output.write(struct.pack("<II", FX3_ENTRY, 0))

    (args.output / "dtu351-fpga.bin").write_bytes(array(source, "PLD_FIRMWARE_DTU351"))
    print(f"FX3 records: {sum(len(payload) for _, payload in records)} bytes")
    print(f"FPGA image: {(args.output / 'dtu351-fpga.bin').stat().st_size} bytes")


if __name__ == "__main__":
    main()
