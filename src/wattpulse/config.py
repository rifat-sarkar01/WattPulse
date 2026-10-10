"""Constants and paths. No logic."""
import os
import sys
from pathlib import Path

APP_NAME = "WattPulse"
APP_ID = "WattPulse.App"
MUTEX_NAME = "Local\\WattPulse.SingleInstance"
DATA_DIR_NAME = APP_NAME
LEGACY_DATA_DIR_NAME = "Wattly"      # pre-rename installs

SAMPLE_INTERVAL = 1.0      # seconds between sensor reads
MAX_GAP = 5.0              # a longer gap (sleep/resume) is not counted as usage
HISTORY_POINTS = 300       # chart length: 5 minutes at 1 Hz
FLUSH_EVERY = 30           # ticks (seconds) between database writes
TOTALS_EVERY = 5           # ticks between totals refreshes
KEEP_DAYS = 35

RANGES = (
    ("Last hour", 3600),
    ("Last 24 hours", 24 * 3600),
    ("Last 7 days", 7 * 24 * 3600),
    ("Last 30 days", 30 * 24 * 3600),
)


def _base_dir() -> Path:
    return Path(os.environ.get("APPDATA") or os.path.expanduser("~"))


def data_dir() -> Path:
    """Per-user data directory. Migrates the pre-rename folder once, if present."""
    path = _base_dir() / DATA_DIR_NAME
    legacy = _base_dir() / LEGACY_DATA_DIR_NAME
    if not path.exists() and legacy.is_dir():
        try:
            legacy.rename(path)
        except OSError:
            pass                      # fall through: just create an empty dir
    path.mkdir(parents=True, exist_ok=True)
    return path


def bundled_icon() -> Path:
    """Best-effort path to the multi-size .ico, or a non-existent path.

    Frozen builds unpack --add-data into sys._MEIPASS; a source checkout keeps
    the icon in <repo>/assets. Either way the caller falls back to the
    procedurally rendered mark when this does not exist.
    """
    if getattr(sys, "frozen", False):                      # PyInstaller
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent)) / "wattpulse.ico"
    return Path(__file__).resolve().parents[2] / "assets" / "wattpulse.ico"
