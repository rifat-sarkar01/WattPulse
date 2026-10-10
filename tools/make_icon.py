"""Generate the WattPulse brand assets (assets/wattpulse.ico + assets/wattpulse.png).

    python tools/make_icon.py

Standard library only: the mark is rendered by wattpulse.ui.icon and packed
straight into a PNG-compressed multi-size .ico (256/64/32/16) plus a 256 px PNG
used by the README.
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "src"))

from wattpulse.ui.icon import render_rgba  # noqa: E402

SIZES = (256, 64, 32, 16)
PNG_SIZE = 256


def png(size: int, rgba: bytes) -> bytes:
    raw = b"".join(b"\x00" + rgba[y * size * 4:(y + 1) * size * 4] for y in range(size))

    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ico(images) -> bytes:
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, data in images:
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    return out + b"".join(data for _, data in images)


def main() -> None:
    assets = ROOT / "assets"
    assets.mkdir(exist_ok=True)
    images = [(s, png(s, render_rgba(s))) for s in SIZES]
    ico_path = assets / "wattpulse.ico"
    ico_path.write_bytes(ico(images))
    png_path = assets / "wattpulse.png"
    png_path.write_bytes(png(PNG_SIZE, render_rgba(PNG_SIZE, samples=3)))
    print(f"wrote {ico_path} ({ico_path.stat().st_size} bytes)")
    print(f"wrote {png_path} ({png_path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
