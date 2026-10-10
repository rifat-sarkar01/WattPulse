"""Windows-only shell integration. Every function is a safe no-op elsewhere."""
import ctypes
import sys

from .config import APP_ID, MUTEX_NAME

WIN = sys.platform == "win32"
ERROR_ALREADY_EXISTS = 183
_mutex = None   # keep the handle alive for the life of the process


def prepare_process() -> None:
    """Crisp rendering on high-DPI screens and correct taskbar grouping/pinning."""
    if not WIN:
        return
    try:
        # Per-monitor v2 (Win10 1703+): no blurry bitmap scaling when the window
        # moves between monitors with different scale factors.
        user32 = ctypes.windll.user32
        if hasattr(user32, "SetProcessDpiAwarenessContext"):
            user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except Exception:
        pass
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass
    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except Exception:
        pass
    try:
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(APP_ID)
    except Exception:
        pass


def acquire_single_instance() -> bool:
    """False if another copy is already running (it would double-count energy)."""
    global _mutex
    if not WIN:
        return True
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateMutexW.restype = ctypes.c_void_p
    k32.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_wchar_p]
    _mutex = k32.CreateMutexW(None, False, MUTEX_NAME)
    return ctypes.get_last_error() != ERROR_ALREADY_EXISTS


def dark_titlebar(root) -> None:
    if not WIN:
        return
    try:
        user32, dwm = ctypes.windll.user32, ctypes.windll.dwmapi
        user32.GetParent.restype = ctypes.c_void_p
        user32.GetParent.argtypes = [ctypes.c_void_p]
        dwm.DwmSetWindowAttribute.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                              ctypes.c_void_p, ctypes.c_uint32]
        root.update_idletasks()
        hwnd = user32.GetParent(root.winfo_id())
        one = ctypes.c_int(1)
        for attribute in (20, 19):               # 20: Win10 20H1+/11, 19: older builds
            if dwm.DwmSetWindowAttribute(hwnd, attribute, ctypes.byref(one), 4) == 0:
                break
    except Exception:
        pass
