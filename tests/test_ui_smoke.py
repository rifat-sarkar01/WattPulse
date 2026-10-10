import tkinter as tk

import pytest

from wattpulse.sampler import Sampler
from wattpulse.sensors.base import Reading, State
from wattpulse.sensors.demo import DemoSensor
from wattpulse.storage import Store


@pytest.fixture()
def root():
    try:
        r = tk.Tk()
    except tk.TclError:
        pytest.skip("no display available")
    yield r
    try:
        r.destroy()
    except tk.TclError:
        pass


def test_window_builds_and_updates(root, tmp_path):
    from wattpulse.ui.window import App

    sampler = Sampler(DemoSensor())
    store = Store(tmp_path / "t.db")
    app = App(root, sampler, store)
    for _ in range(3):
        sampler.ingest(DemoSensor().read(), 1.0)
    app._tick()
    root.update()
    assert app.value.cget("text") not in ("", "\u2014")
    app.toggle_view()                  # compact
    app.toggle_view()                  # back to expanded
    app.toggle_top()
    app.flush()
    assert store.total(3600) > 0


def test_states_render(root, tmp_path):
    from wattpulse.ui.window import STATE_TEXT, App

    sampler = Sampler(DemoSensor())
    app = App(root, sampler, Store(tmp_path / "t.db"))
    for state in State:
        sampler.ingest(Reading(state, None, None), 1.0)
        app._tick()
        assert app.caption.cget("text") == STATE_TEXT[state]
    root.update()
