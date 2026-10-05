"""Bounded verification of native, non-interlaced screenshot PNG artifacts."""

from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import zlib

MAX_BYTES = 256 * 1024 * 1024
MAX_PIXELS = 64 * 1024 * 1024
CHANNELS = dict(((0, 1), (2, 3), (4, 2), (6, 4)))


def inspect_png(path: Path) -> dict:
    size = path.stat().st_size
    if size < 45 or size > MAX_BYTES:
        raise ValueError("PNG size is outside the screenshot budget")
    image = path.read_bytes()
    if image[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("invalid PNG signature")
    offset, header, data, ended = 8, None, bytearray(), False
    while offset < len(image):
        if offset + 12 > len(image):
            raise ValueError("truncated PNG chunk")
        length = struct.unpack_from(">I", image, offset)[0]
        end = offset + 12 + length
        if end > len(image):
            raise ValueError("truncated PNG payload")
        kind, body = image[offset + 4:offset + 8], image[offset + 8:end - 4]
        crc = struct.unpack_from(">I", image, end - 4)[0]
        if zlib.crc32(kind + body) & 0xFFFFFFFF != crc:
            raise ValueError("PNG chunk CRC mismatch")
        if header is None and kind != b"IHDR":
            raise ValueError("PNG must begin with IHDR")
        if kind == b"IHDR":
            if header is not None or length != 13:
                raise ValueError("invalid PNG header")
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            data.extend(body)
        elif kind == b"IEND":
            if length or end != len(image):
                raise ValueError("invalid PNG end")
            ended = True
        offset = end
    if not header or not ended or not data:
        raise ValueError("incomplete PNG")
    width, height, depth, color, compression, filtering, interlace = header
    if not width or not height or width * height > MAX_PIXELS:
        raise ValueError("invalid PNG dimensions")
    if depth not in (8, 16) or color not in CHANNELS or compression or filtering or interlace:
        raise ValueError("unsupported screenshot PNG encoding")
    row = width * CHANNELS[color] * depth // 8 + 1
    expected = row * height
    if expected > MAX_BYTES:
        raise ValueError("PNG decoded data exceeds budget")
    decoder = zlib.decompressobj()
    pixels = decoder.decompress(data, expected + 1)
    if len(pixels) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("invalid PNG pixel stream")
    if any(pixels[index] > 4 for index in range(0, expected, row)):
        raise ValueError("invalid PNG scanline filter")
    return dict(width=width, height=height, size_bytes=size,
                sha1=hashlib.sha1(image).hexdigest(), sha256=hashlib.sha256(image).hexdigest())
