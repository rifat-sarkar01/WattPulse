"""Main window: header, hero reading, and an expandable detail panel."""
import sqlite3
import tkinter as tk
from typing import Callable, Optional

from .. import platform_win
from ..config import APP_NAME, FLUSH_EVERY, RANGES, TOTALS_EVERY
from ..format import DASH, fmt_energy, fmt_watts
from ..sampler import Sampler
from ..sensors.base import State
from ..storage import Store
from . import theme
from .chart import Chart
from .icon import make_photo_image, window_icon

STATE_TEXT = {
    State.DISCHARGING: "On battery",
    State.CHARGING: "Charging \u00b7 power into battery",
    State.AC: "On AC power",
    State.NONE: "No battery sensor found",
}


class App:
    def __init__(self, root: tk.Tk, sampler: Sampler, store: Store) -> None:
        self.root, self.sampler, self.store = root, sampler, store
        self.compact, self.on_top, self.ticks = False, False, 0
        self.k = root.winfo_fpixels("1i") / 96.0

        root.title(APP_NAME)
        root.configure(bg=theme.BG)
        self._icon = window_icon(root)          # keep a reference for as long as the window lives
        root.protocol("WM_DELETE_WINDOW", self.close)

        self._build_header()
        self._build_hero()
        self._build_detail()
        self._apply_view()
        platform_win.dark_titlebar(root)
        self._tick()

    # layout ----------------------------------------------------------------
    def px(self, v: float) -> int:
        return int(round(v * self.k))

    def _link(self, parent: tk.Misc, text: str, command: Callable[[], None]) -> tk.Label:
        label = tk.Label(parent, text=text, bg=theme.BG, fg=theme.MUTED,
                         cursor="hand2", font=theme.F_LINK)
        label.bind("<Button-1>", lambda _e: command())
        label.bind("<Enter>", lambda _e: label.config(fg=theme.TEXT))
        label.bind("<Leave>", lambda _e: label.config(
            fg=theme.ACCENT if (label is self.b_top and self.on_top) else theme.MUTED))
        return label

    def _section(self, parent: tk.Misc, text: str, top: int, bottom: int) -> None:
        tk.Label(parent, text=text, bg=theme.BG, fg=theme.MUTED, font=theme.F_LABEL).pack(
            anchor="w", padx=self.px(24), pady=(self.px(top), self.px(bottom)))

    def _build_header(self) -> None:
        px = self.px
        header = tk.Frame(self.root, bg=theme.BG)
        header.pack(fill="x", padx=px(24), pady=(px(18), 0))
        left = tk.Frame(header, bg=theme.BG)
        left.pack(side="left")
        self._brand = make_photo_image(px(16))       # keep a reference
        tk.Label(left, image=self._brand, bg=theme.BG).pack(side="left", padx=(0, px(8)))
        tk.Label(left, text="POWER DRAW", bg=theme.BG, fg=theme.MUTED,
                 font=theme.F_LABEL).pack(side="left")
        self.b_top = self._link(header, "Pin on top", self.toggle_top)
        self.b_view = self._link(header, "Compact", self.toggle_view)
        self.b_top.pack(side="right", padx=(px(14), 0))
        self.b_view.pack(side="right")

    def _build_hero(self) -> None:
        px = self.px
        hero = tk.Frame(self.root, bg=theme.BG)
        hero.pack(fill="x", padx=px(24), pady=(px(6), 0))
        row = tk.Frame(hero, bg=theme.BG)
        row.pack(anchor="w")
        self.value = tk.Label(row, text=DASH, bg=theme.BG, fg=theme.TEXT, font=theme.F_HERO)
        self.value.pack(side="left")
        self.unit = tk.Label(row, text="", bg=theme.BG, fg=theme.MUTED, font=theme.F_UNIT)
        self.unit.pack(side="left", anchor="s", pady=(0, px(14)), padx=(px(6), 0))
        self.caption = tk.Label(hero, text="", bg=theme.BG, fg=theme.MUTED,
                                font=theme.F_BODY, anchor="w")
        self.caption.pack(anchor="w")

    def _build_detail(self) -> None:
        px = self.px
        self.detail = tk.Frame(self.root, bg=theme.BG)
        self._section(self.detail, "LAST 5 MINUTES", 18, 6)
        self.chart = Chart(self.detail, self.k)
        self.chart.canvas.pack(fill="both", expand=True, padx=px(24))
        self._section(self.detail, "ENERGY USED", 18, 2)
        self.totals = []
        for name, _ in RANGES:
            tk.Frame(self.detail, bg=theme.LINE, height=1).pack(fill="x", padx=px(24))
            row = tk.Frame(self.detail, bg=theme.BG)
            row.pack(fill="x", padx=px(24), pady=px(9))
            tk.Label(row, text=name, bg=theme.BG, fg=theme.MUTED, font=theme.F_BODY).pack(side="left")
            label = tk.Label(row, text=DASH, bg=theme.BG, fg=theme.TEXT, font=theme.F_VALUE)
            label.pack(side="right")
            self.totals.append(label)
        tk.Frame(self.detail, bg=theme.LINE, height=1).pack(fill="x", padx=px(24))
        tk.Label(self.detail, text="Totals count time spent on battery only.", bg=theme.BG,
                 fg=theme.MUTED, font=theme.F_SMALL).pack(anchor="w", padx=px(24), pady=(px(8), px(14)))

    # view state ------------------------------------------------------------
    def _apply_view(self) -> None:
        px = self.px
        if self.compact:
            self.detail.pack_forget()
            self.root.minsize(px(300), px(160))
            self.root.geometry(f"{px(300)}x{px(165)}")
            self.root.resizable(False, False)
            self.b_view.config(text="Expand")
        else:
            self.detail.pack(fill="both", expand=True)
            self.root.resizable(True, True)
            self.root.minsize(px(340), px(520))
            self.root.geometry(f"{px(400)}x{px(590)}")
            self.b_view.config(text="Compact")
            self._refresh_totals()

    def toggle_view(self) -> None:
        self.compact = not self.compact
        self._apply_view()

    def toggle_top(self) -> None:
        self.on_top = not self.on_top
        self.root.attributes("-topmost", self.on_top)
        self.b_top.config(fg=theme.ACCENT if self.on_top else theme.MUTED)

    # updates ---------------------------------------------------------------
    def _refresh_totals(self) -> None:
        pending = self.sampler.pending()
        for label, (_, seconds) in zip(self.totals, RANGES):
            label.config(text=fmt_energy(self.store.total(seconds, pending)))

    def flush(self) -> None:
        rows = self.sampler.drain()
        try:
            self.store.write(rows)
        except sqlite3.Error:
            self.sampler.restore(rows)           # keep it; retry next time

    @staticmethod
    def smoothed(history) -> Optional[float]:
        if not history or history[-1] is None:
            return None
        recent = [v for v in history[-3:] if v is not None]
        return sum(recent) / len(recent)

    def _tick(self) -> None:
        try:
            snap = self.sampler.snapshot()
            watts = self.smoothed(snap.history)
            self.value.config(text=fmt_watts(watts),
                              fg=theme.CHARGING if snap.state is State.CHARGING else theme.TEXT)
            self.unit.config(text="W" if watts is not None else "")
            pct = f"  \u00b7  {snap.percent}%" if snap.percent is not None else ""
            self.caption.config(text=STATE_TEXT[snap.state] + pct)
            self.root.title(f"{fmt_watts(watts)} W" if watts is not None else APP_NAME)
            if not self.compact and self.root.state() != "iconic":
                self.chart.draw(snap.history)
                if self.ticks % TOTALS_EVERY == 0:
                    self._refresh_totals()
            if self.ticks % FLUSH_EVERY == FLUSH_EVERY - 1:
                self.flush()
            self.ticks += 1
        except Exception:
            pass                                  # never let one bad frame stop the loop
        finally:
            self.root.after(1000, self._tick)

    def close(self) -> None:
        self.sampler.stop()
        try:
            self.flush()
        finally:
            try:
                self.store.close()
            finally:
                self.root.destroy()
