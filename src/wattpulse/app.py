"""Entry point: argument parsing, wiring, and last-resort error reporting."""
import argparse
import sys
import time
import traceback
from datetime import datetime
from typing import Optional, Sequence

from . import __version__, platform_win
from .config import APP_NAME, data_dir
from .sampler import Sampler
from .sensors import create_sensor
from .storage import Store


def _report(exc_type, exc, tb) -> None:
    """Windowed launches (pythonw, PyInstaller --noconsole) have no console:
    write a log and show a message box instead of dying silently."""
    text = "".join(traceback.format_exception(exc_type, exc, tb))
    if sys.stderr is not None:                     # a console is attached: keep the default
        sys.__excepthook__(exc_type, exc, tb)
        return
    log = None
    try:
        log = data_dir() / "crash.log"
        log.write_text(f"{datetime.now():%Y-%m-%d %H:%M:%S}\n{text}", encoding="utf-8")
    except Exception:
        pass
    if sys.platform == "win32":
        detail = f"Details were saved to:\n{log}" if log else ""
        message = f"{APP_NAME} hit an unexpected error:\n\n{text[-1200:]}\n\n{detail}"
        try:
            import ctypes

            ctypes.windll.user32.MessageBoxW(None, message, APP_NAME, 0x10)
        except Exception:
            pass


def _probe() -> int:
    sensor = create_sensor()
    try:
        print(f"batteries: {getattr(sensor, 'battery_count', 0)}")
        for _ in range(5):
            try:
                print(sensor.read())
            except Exception as exc:
                print(f"read failed: {exc}")
            time.sleep(1)
    finally:
        sensor.close()
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    sys.excepthook = _report
    parser = argparse.ArgumentParser(prog="wattpulse", description="Live device power draw.")
    parser.add_argument("--demo", action="store_true", help="show synthetic data (nothing is saved)")
    parser.add_argument("--probe", action="store_true", help="print raw sensor readings and exit")
    parser.add_argument("--version", action="version", version=f"{APP_NAME} {__version__}")
    args = parser.parse_args(argv)

    if args.probe:
        return _probe()

    platform_win.prepare_process()
    if not platform_win.acquire_single_instance():
        return 0

    import tkinter as tk

    from .ui.window import App

    sensor = create_sensor(demo=args.demo)
    sampler = Sampler(sensor)
    store = Store(":memory:" if args.demo else data_dir() / "power.db")
    try:
        root = tk.Tk()
        App(root, sampler, store)
        sampler.start()
        root.mainloop()
    finally:
        sampler.stop()
        sensor.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
