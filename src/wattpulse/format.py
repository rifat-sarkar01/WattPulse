"""Pure formatting helpers."""
import math
from typing import Optional

DASH = "\u2014"


def fmt_watts(w: Optional[float]) -> str:
    if w is None:
        return DASH
    return f"{w:.1f}" if w < 100 else f"{w:.0f}"


def fmt_energy(mwh: float) -> str:
    wh = mwh / 1000.0
    if wh >= 1000:
        return f"{wh / 1000:.2f} kWh"
    return f"{wh:.0f} Wh" if wh >= 100 else f"{wh:.1f} Wh"


_STEPS = (5, 10, 15, 20, 30, 40, 50, 75, 100, 150, 200, 300)


def nice_ceiling(v: float) -> int:
    """Smallest 'round' axis maximum that fits v."""
    for step in _STEPS:
        if v <= step:
            return step
    return int(math.ceil(v / 100.0) * 100)
