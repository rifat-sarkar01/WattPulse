"""Background sampler: reads the sensor once a second and integrates energy."""
import collections
import threading
import time
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional

from .config import HISTORY_POINTS, MAX_GAP, SAMPLE_INTERVAL
from .sensors.base import Reading, Sensor, State


@dataclass(frozen=True)
class Snapshot:
    state: State
    history: List[Optional[float]]   # watts, None = no measurement
    percent: Optional[int]


class Sampler:
    def __init__(self, sensor: Sensor, interval: float = SAMPLE_INTERVAL,
                 history: int = HISTORY_POINTS,
                 wall: Callable[[], float] = time.time) -> None:
        self._sensor = sensor
        self._interval = interval
        self._wall = wall
        self._lock = threading.Lock()
        self._hist: collections.deque = collections.deque(maxlen=history)
        self._state, self._percent = State.NONE, None
        self._pending: Dict[int, float] = {}          # minute timestamp -> mWh
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None

    # lifecycle -------------------------------------------------------------
    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="sampler", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)

    def _run(self) -> None:
        last = time.monotonic()
        while True:
            try:
                reading = self._sensor.read()
            except Exception:
                reading = Reading(State.NONE)
            now = time.monotonic()
            dt, last = now - last, now
            self.ingest(reading, dt if dt <= MAX_GAP else self._interval)
            if self._stop.wait(self._interval):
                break

    # data ------------------------------------------------------------------
    def ingest(self, reading: Reading, dt: float) -> None:
        mw = reading.milliwatts
        with self._lock:
            self._state, self._percent = reading.state, reading.percent
            self._hist.append(None if mw is None else mw / 1000.0)
            if reading.state is State.DISCHARGING and mw is not None:
                minute = int(self._wall() // 60 * 60)
                self._pending[minute] = self._pending.get(minute, 0.0) + mw * dt / 3600.0

    def snapshot(self) -> Snapshot:
        with self._lock:
            return Snapshot(self._state, list(self._hist), self._percent)

    def pending(self) -> Dict[int, float]:
        with self._lock:
            return dict(self._pending)

    def drain(self) -> Dict[int, float]:
        with self._lock:
            rows, self._pending = self._pending, {}
            return rows

    def restore(self, rows: Dict[int, float]) -> None:
        with self._lock:
            for ts, v in rows.items():
                self._pending[ts] = self._pending.get(ts, 0.0) + v
