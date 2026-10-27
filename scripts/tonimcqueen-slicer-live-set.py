#!/usr/bin/env python3
"""Send one fixed-size live setting command to a running ToniMcQueen-Slicer."""

import argparse
import socket
import struct
import os

MAGIC = 0x544D5143
VERSION = 1


def float_bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("setting", choices=("prime-tower", "wall-loops", "outer-wall-width", "layer-height"))
    parser.add_argument("value")
    parser.add_argument("--socket", default=f"/tmp/tonimcqueen-slicer-{os.getuid()}.sock")
    args = parser.parse_args()

    if args.setting == "prime-tower":
        opcode, bits = 1, int(args.value.lower() in ("1", "true", "on"))
    elif args.setting == "wall-loops":
        opcode, bits = 2, int(args.value)
    elif args.setting == "outer-wall-width":
        opcode, bits = 3, float_bits(float(args.value))
    else:
        opcode, bits = 4, float_bits(float(args.value))

    packet = struct.pack("<IHHI", MAGIC, VERSION, opcode, bits)
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.connect(args.socket)
        connection.sendall(packet)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
