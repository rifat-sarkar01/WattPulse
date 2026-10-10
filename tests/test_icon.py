"""The brand mark rendered by wattpulse.ui.icon."""
from wattpulse.ui import theme
from wattpulse.ui.icon import PULSE, render_rgba, render_samples, stroke_width


def rgba_at(rgba: bytes, size: int, x: int, y: int) -> bytes:
    i = (y * size + x) * 4
    return bytes(rgba[i:i + 4])


def test_geometry_matches_the_design_reference():
    assert PULSE[0] == (8, 38) and PULSE[-1] == (56, 38)
    assert max(x for x, _ in PULSE) <= 64 and max(y for _, y in PULSE) <= 64


def test_palette_matches_the_brand_sheet():
    assert theme.INK == "#0E1424"
    assert theme.VOLT == "#F68E17"
    assert theme.PAPER == "#EEF0E8"
    assert theme.ACCENT == theme.VOLT and theme.ICON_TILE == theme.INK


def test_stroke_thickens_for_small_sizes():
    assert stroke_width(128) == 5.0
    assert stroke_width(64) == 5.0
    assert stroke_width(32) == 6.0
    assert stroke_width(20) == 7.0
    assert stroke_width(16) == 8.0


def test_transparent_corner_and_volt_pulse_at_64px():
    size = 64
    rgba = render_rgba(size)
    assert len(rgba) == size * size * 4
    assert rgba_at(rgba, size, 0, 0) == b"\x00\x00\x00\x00"          # outside the rounded tile
    assert rgba_at(rgba, size, 63, 63) == b"\x00\x00\x00\x00"
    assert rgba_at(rgba, size, 14, 38) == bytes((0xF6, 0x8E, 0x17, 255))   # on the pulse
    assert rgba_at(rgba, size, 10, 32) == bytes((0x0E, 0x14, 0x24, 255))   # ink tile


def test_every_rendered_pixel_is_brand_colour_or_clear():
    grid = render_samples(64)
    allowed = {"", theme.ACCENT, theme.ICON_TILE}
    assert {c for row in grid for c in row} <= allowed
    assert any(c == theme.ACCENT for row in grid for c in row)
    assert any(c == theme.ICON_TILE for row in grid for c in row)
