"""SQLite persistence: one row per minute holding milliwatt-hours used."""
import sqlite3
import time
from pathlib import Path
from typing import Callable, Mapping, Optional, Union

from .config import KEEP_DAYS


class Store:
    def __init__(self, path: Union[str, Path], keep_days: int = KEEP_DAYS,
                 wall: Callable[[], float] = time.time) -> None:
        self._wall = wall
        self._db = sqlite3.connect(str(path))
        self._db.execute("PRAGMA journal_mode=WAL")
        self._db.execute("PRAGMA synchronous=NORMAL")
        self._db.execute("CREATE TABLE IF NOT EXISTS energy (ts INTEGER PRIMARY KEY, mwh REAL NOT NULL)")
        self._db.execute("DELETE FROM energy WHERE ts < ?", (int(wall()) - keep_days * 86400,))
        self._db.commit()

    def write(self, rows: Mapping[int, float]) -> None:
        """Add energy to minute buckets. Raises sqlite3.Error on failure."""
        if not rows:
            return
        self._db.executemany(
            "INSERT INTO energy(ts, mwh) VALUES(?, ?) "
            "ON CONFLICT(ts) DO UPDATE SET mwh = mwh + excluded.mwh",
            list(rows.items()))
        self._db.commit()

    def total(self, seconds: int, extra: Optional[Mapping[int, float]] = None) -> float:
        """Milliwatt-hours used in the last `seconds`, including unsaved `extra`."""
        cut = int(self._wall()) - seconds
        try:
            stored = self._db.execute(
                "SELECT COALESCE(SUM(mwh), 0) FROM energy WHERE ts >= ?", (cut,)).fetchone()[0]
        except sqlite3.Error:
            stored = 0.0
        unsaved = sum(v for ts, v in (extra or {}).items() if ts >= cut)
        return stored + unsaved

    def close(self) -> None:
        self._db.close()
