"""Battery power via the Windows battery driver (IOCTL_BATTERY_QUERY_STATUS).

This is the same source Windows' own battery meter uses. Rate is reported in
milliwatts; the sign convention varies by vendor, so the magnitude is used and
the direction comes from the power-state flags.
"""
import ctypes
import struct
import time
from ctypes import wintypes as wt
from typing import List, Optional, Tuple

from .base import Reading, State

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
setupapi = ctypes.WinDLL("setupapi", use_last_error=True)
P = ctypes.c_void_p
INVALID = P(-1).value

IOCTL_TAG, IOCTL_STATUS = 0x294040, 0x29404C
UNKNOWN_RATE = -2147483648
PS_DISCHARGING, PS_CHARGING = 0x2, 0x4
RESCAN_AFTER_ERROR, RESCAN_WHEN_EMPTY = 5.0, 60.0


class GUID(ctypes.Structure):
    _fields_ = [("a", ctypes.c_uint32), ("b", ctypes.c_uint16),
                ("c", ctypes.c_uint16), ("d", ctypes.c_ubyte * 8)]


class IFACE(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("guid", GUID), ("flags", wt.DWORD), ("res", P)]


class WAIT_STATUS(ctypes.Structure):
    _fields_ = [("tag", wt.ULONG), ("timeout", wt.ULONG), ("ps", wt.ULONG),
                ("lo", wt.ULONG), ("hi", wt.ULONG)]


class BATTERY_STATUS(ctypes.Structure):
    _fields_ = [("ps", wt.ULONG), ("cap", wt.ULONG), ("volt", wt.ULONG), ("rate", wt.LONG)]


class SYSTEM_POWER_STATUS(ctypes.Structure):
    _fields_ = [("ac", ctypes.c_ubyte), ("flag", ctypes.c_ubyte), ("pct", ctypes.c_ubyte),
                ("sf", ctypes.c_ubyte), ("life", wt.DWORD), ("full", wt.DWORD)]


GUID_BATTERY = GUID(0x72631E54, 0x78A4, 0x11D0,
                    (ctypes.c_ubyte * 8)(0xBC, 0xF7, 0x00, 0xAA, 0x00, 0xB7, 0xB3, 0x2A))

setupapi.SetupDiGetClassDevsW.restype = P
setupapi.SetupDiGetClassDevsW.argtypes = [ctypes.POINTER(GUID), wt.LPCWSTR, P, wt.DWORD]
setupapi.SetupDiEnumDeviceInterfaces.argtypes = [P, P, ctypes.POINTER(GUID), wt.DWORD, ctypes.POINTER(IFACE)]
setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [
    P, ctypes.POINTER(IFACE), P, wt.DWORD, ctypes.POINTER(wt.DWORD), P]
setupapi.SetupDiDestroyDeviceInfoList.argtypes = [P]
k32.CreateFileW.restype = P
k32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, P, wt.DWORD, wt.DWORD, P]
k32.DeviceIoControl.argtypes = [P, wt.DWORD, P, wt.DWORD, P, wt.DWORD, ctypes.POINTER(wt.DWORD), P]
k32.CloseHandle.argtypes = [P]


def _percent() -> Optional[int]:
    s = SYSTEM_POWER_STATUS()
    if k32.GetSystemPowerStatus(ctypes.byref(s)) and s.pct <= 100:
        return int(s.pct)
    return None


class BatterySensor:
    def __init__(self) -> None:
        self._devs: List[Tuple[int, int]] = []   # (handle, tag)
        self._dirty = False
        self._t_scan = 0.0
        self._scan()

    @property
    def battery_count(self) -> int:
        return len(self._devs)

    def close(self) -> None:
        for handle, _ in self._devs:
            k32.CloseHandle(handle)
        self._devs = []

    def _scan(self) -> None:
        self.close()
        self._dirty = False
        self._t_scan = time.monotonic()
        hdev = setupapi.SetupDiGetClassDevsW(ctypes.byref(GUID_BATTERY), None, None, 0x12)
        if hdev in (None, INVALID):
            return
        try:
            index = 0
            while True:
                di = IFACE()
                di.cb = ctypes.sizeof(IFACE)
                if not setupapi.SetupDiEnumDeviceInterfaces(
                        hdev, None, ctypes.byref(GUID_BATTERY), index, ctypes.byref(di)):
                    break
                index += 1
                self._open(hdev, di)
        finally:
            setupapi.SetupDiDestroyDeviceInfoList(hdev)

    def _open(self, hdev: int, di: IFACE) -> None:
        need = wt.DWORD(0)
        setupapi.SetupDiGetDeviceInterfaceDetailW(hdev, ctypes.byref(di), None, 0, ctypes.byref(need), None)
        if need.value == 0:
            return
        buf = ctypes.create_string_buffer(need.value)
        struct.pack_into("I", buf, 0, 8 if ctypes.sizeof(P) == 8 else 6)
        if not setupapi.SetupDiGetDeviceInterfaceDetailW(hdev, ctypes.byref(di), buf, need.value, None, None):
            return
        path = ctypes.wstring_at(ctypes.addressof(buf) + 4)
        handle = k32.CreateFileW(path, 0xC0000000, 3, None, 3, 0x80, None)
        if handle in (None, INVALID):
            return
        tag, zero, n = wt.DWORD(0), wt.DWORD(0), wt.DWORD(0)
        ok = k32.DeviceIoControl(handle, IOCTL_TAG, ctypes.byref(zero), 4,
                                 ctypes.byref(tag), 4, ctypes.byref(n), None)
        if ok and tag.value:
            self._devs.append((handle, tag.value))
        else:
            k32.CloseHandle(handle)

    def read(self) -> Reading:
        gap = time.monotonic() - self._t_scan
        if (self._dirty and gap > RESCAN_AFTER_ERROR) or (not self._devs and gap > RESCAN_WHEN_EMPTY):
            self._scan()
        pct = _percent()
        if not self._devs:
            return Reading(State.NONE, None, pct)

        flags, mw, known, answered = 0, 0, False, 0
        for handle, tag in self._devs:
            wait, st, n = WAIT_STATUS(tag, 0, 0, 0, 0), BATTERY_STATUS(), wt.DWORD(0)
            if not k32.DeviceIoControl(handle, IOCTL_STATUS, ctypes.byref(wait), ctypes.sizeof(wait),
                                       ctypes.byref(st), ctypes.sizeof(st), ctypes.byref(n), None):
                self._dirty = True
                continue
            answered += 1
            flags |= st.ps
            if st.rate != UNKNOWN_RATE:
                mw += abs(st.rate)
                known = True
        if not answered:
            # every battery rejected the query: we know nothing, so do not claim AC power
            return Reading(State.NONE, None, pct)
        value = mw if known and mw > 0 else None
        if flags & PS_DISCHARGING:
            return Reading(State.DISCHARGING, value, pct)
        if flags & PS_CHARGING:
            return Reading(State.CHARGING, value, pct)
        return Reading(State.AC, None, pct)
