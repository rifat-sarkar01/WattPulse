"""Live line chart drawn on a tk.Canvas."""
import tkinter as tk
from typing import List, Optional

from ..config import HISTORY_POINTS
from ..format import nice_ceiling
from . import theme


class Chart:
    def __init__(self, parent: tk.Misc, scale: float) -> None:
        self.k = scale
        self.canvas = tk.Canvas(parent, bg=theme.BG, highlightthickness=0, height=self._px(150))
        self._last: List[Optional[float]] = []
        self.canvas.bind("<Configure>", lambda _e: self.draw(self._last))

    def _px(self, v: float) -> int:
        return int(round(v * self.k))

    def draw(self, values: List[Optional[float]]) -> None:
        self._last = values
        c = self.canvas
        c.delete("all")
        w, h = c.winfo_width(), c.winfo_height()
        if w < 60 or h < 40:
            return
        px = self._px
        x0, x1, y0, y1 = px(30), w - px(2), px(8), h - px(6)
        top = nice_ceiling(max((v for v in values if v is not None), default=0.0) * 1.15)

        for frac in (0, .5, 1):
            y = y1 - (y1 - y0) * frac
            c.create_line(x0, y, x1, y, fill=theme.LINE)
            c.create_text(x0 - px(8), y, text=f"{top * frac:g}", fill=theme.MUTED,
                          anchor="e", font=theme.F_SMALL)

        offset = HISTORY_POINTS - len(values)

        def point(i: int, v: float):
            return (x0 + (x1 - x0) * (i + offset) / (HISTORY_POINTS - 1),
                    y1 - (y1 - y0) * min(v / top, 1.0))

        segment = []
        for i, v in enumerate(list(values) + [None]):      # trailing None flushes the last run
            if v is not None:
                segment.append(point(i, v))
                continue
            if len(segment) >= 2:
                flat = [q for p in segment for q in p]
                c.create_polygon(flat + [segment[-1][0], y1, segment[0][0], y1],
                                 fill=theme.ACCENT_FILL, outline="")
                c.create_line(flat, fill=theme.ACCENT, width=max(1.5, px(1.5)), joinstyle="round")
            segment = []

        if values and values[-1] is not None:
            x, y = point(len(values) - 1, values[-1])
            r = px(3)
            c.create_oval(x - r, y - r, x + r, y + r, fill=theme.ACCENT, outline=theme.BG)
