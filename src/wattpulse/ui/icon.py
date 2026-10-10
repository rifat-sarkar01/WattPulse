"""The WattPulse app mark, rendered from the brand sheet design reference.

The reference artboard defines the logo as a 64x64 rounded tile plus one pulse
polyline; the same geometry is used here so the window, taskbar and .ico all
match the brand sheet:

    <rect width="64" height="64" rx="16" fill="#0E1424"/>
    <path d="M8 38H20L29 13L37 51L44 38H56" stroke="#F68E17"
          stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>
"""
from __future__ import annotations

from typing import List, Sequence, Tuple

from ..config import bundled_icon
from . import theme

VIEW = 64.0                     # design units across the mark
TILE_RADIUS = 16.0              # rx of the rounded tile (25% of the mark)
PULSE: Tuple[Tuple[float, float], ...] = (
    (8, 38), (20, 38), (29, 13), (37, 51), (44, 38), (56, 38),
)


def stroke_width(units: float) -> float:
    """Stroke weight in design units: the reference thickens it for small taskbar sizes."""
    if units <= 16:
        return 8.0
    if units <= 24:
        return 7.0
    if units <= 32:
        return 6.0
    return 5.0


def _in_tile(x: float, y: float) -> bool:
    """Inside the rounded square? Distance-to-inner-rect form of the corner arcs."""
    if x < 0 or y < 0 or x > VIEW or y > VIEW:
        return False
    cx = min(max(x, TILE_RADIUS), VIEW - TILE_RADIUS)
    cy = min(max(y, TILE_RADIUS), VIEW - TILE_RADIUS)
    return (x - cx) ** 2 + (y - cy) ** 2 <= TILE_RADIUS ** 2


def _on_pulse(x: float, y: float, half: float) -> bool:
    """Within `half` design units of the pulse polyline (round caps/joins included)."""
    r2 = half * half
    for (x1, y1), (x2, y2) in zip(PULSE, PULSE[1:]):
        dx, dy = x2 - x1, y2 - y1
        len2 = dx * dx + dy * dy
        t = 0.0 if len2 == 0 else max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / len2))
        px, py = x1 + t * dx, y1 + t * dy
        if (x - px) ** 2 + (y - py) ** 2 <= r2:
            return True
    return False


def _colour_at(x: float, y: float, half: float) -> str:
    """Hex colour of one design-space point, or '' where the mark is transparent."""
    if not _in_tile(x, y):
        return ""
    return theme.ACCENT if _on_pulse(x, y, half) else theme.ICON_TILE


def _hex_rgb(value: str) -> Tuple[int, int, int]:
    return tuple(int(value[i:i + 2], 16) for i in (1, 3, 5))  # type: ignore[return-value]


def render_rgba(size: int, samples: int = 2) -> bytes:
    """Anti-aliased RGBA pixels (used by tools/make_icon.py)."""
    scale = size / VIEW
    half = stroke_width(size) / 2.0
    out = bytearray()
    for py in range(size):
        for px in range(size):
            r = g = b = a = 0
            for sy in range(samples):
                for sx in range(samples):
                    x = (px + (sx + .5) / samples) / scale
                    y = (py + (sy + .5) / samples) / scale
                    colour = _colour_at(x, y, half)
                    if not colour:
                        continue
                    c = _hex_rgb(colour)
                    r, g, b, a = r + c[0], g + c[1], b + c[2], a + 1
            total = samples * samples
            if a:
                out += bytes((r // a, g // a, b // a, 255 * a // total))
            else:
                out += b"\x00\x00\x00\x00"
    return bytes(out)


def render_samples(size: int) -> List[List[str]]:
    """Colour of each pixel centre ('' = transparent). Single sample, exact brand colours."""
    scale = size / VIEW
    half = stroke_width(size) / 2.0
    return [
        [_colour_at((px + .5) / scale, (py + .5) / scale, half) for px in range(size)]
        for py in range(size)
    ]


def make_photo_image(size: int = 32):
    """Square tkinter PhotoImage of the mark, for iconphoto()."""
    import tkinter as tk

    grid = render_samples(size)
    img = tk.PhotoImage(width=size, height=size)
    for y, row in enumerate(grid):
        for x, colour in enumerate(row):
            if colour:
                img.put(colour, to=(x, y))
    return img


def window_icon(root) -> object:
    """Set the window/taskbar icon; returns a PhotoImage the caller must keep alive."""
    photo = make_photo_image()
    root.iconphoto(True, photo)
    ico = bundled_icon()                       # multi-size .ico: crisp Alt-Tab / taskbar art
    if ico.is_file():
        try:
            root.iconbitmap(str(ico))
        except Exception:                      # TclError where .ico is unsupported
            pass
    return photo


__all__: Sequence[str] = (
    "PULSE", "VIEW", "make_photo_image", "render_rgba", "render_samples", "stroke_width",
    "window_icon",
)
