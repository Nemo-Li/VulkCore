#!/usr/bin/env python3
"""Write a seamless RG distortion map for shader_tornado.frag."""

from __future__ import annotations

import struct
import zlib
import math
from pathlib import Path


SIZE = 256


def linear_to_srgb(value: float) -> int:
    value = max(0.0, min(1.0, value))
    encoded = 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055
    return max(0, min(255, round(encoded * 255.0)))


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def generate_pixels() -> bytes:
    rows = bytearray()
    tau = 6.283185307179586
    for y in range(SIZE):
        rows.append(0)  # PNG filter byte: none.
        v = y / SIZE
        for x in range(SIZE):
            u = x / SIZE
            # Every term is periodic in U/V, so the map tiles without a seam.
            a = tau * (u * 3.0 + v * 1.7)
            b = tau * (u * 7.0 - v * 4.0)
            c = tau * (u * 11.0 + v * 9.0)
            dx = 0.5 + 0.27 * math.sin(a) + 0.16 * math.sin(b + 0.7)
            dy = 0.5 + 0.25 * math.cos(a * 0.83 - 0.4) + 0.18 * math.sin(c)
            rows.extend((
                # Texture.cpp uploads all images as sRGB. Encode the
                # displacement values here so shader sampling reconstructs
                # the intended linear 0..1 range around the 0.5 midpoint.
                linear_to_srgb(dx),
                linear_to_srgb(dy),
                255,
                255,
            ))
    return bytes(rows)


def write_png(path: Path) -> None:
    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", header)
    png += png_chunk(b"IDAT", zlib.compress(generate_pixels(), 9))
    png += png_chunk(b"IEND", b"")
    path.write_bytes(png)


if __name__ == "__main__":
    write_png(Path(__file__).with_name("tornado_distortion.png"))
