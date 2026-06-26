"""Leitura de joystick local para posição do coletivo (Linux e Windows)."""

from __future__ import annotations

import os
import struct
import sys

from PySide6.QtCore import QObject, QTimer, Signal


class _JoystickConfigMixin:
    def _load_config(self) -> None:
        axis_env = os.getenv("SCCA_JOYSTICK_AXIS", "")
        self.axis_number = int(axis_env) if axis_env.strip().isdigit() else 0
        self.invert_axis = os.getenv("SCCA_JOYSTICK_INVERT", "0").strip().lower() in {
            "1", "true", "yes", "on",
        }
        self.min_raw = int(os.getenv("SCCA_JOYSTICK_MIN_RAW", "32767"))
        self.max_raw = int(os.getenv("SCCA_JOYSTICK_MAX_RAW", "-32767"))
        self.smoothing_alpha = float(os.getenv("SCCA_JOYSTICK_SMOOTHING_ALPHA", "0.18"))
        self.deadband_percent = float(os.getenv("SCCA_JOYSTICK_DEADBAND_PERCENT", "0.25"))
        self.smoothing_alpha = max(0.01, min(1.0, self.smoothing_alpha))
        self.deadband_percent = max(0.0, min(5.0, self.deadband_percent))
        self._connected = False
        self._position_percent = 0.0
        self._filtered_position_percent = 0.0
        self._has_filtered_position = False
        self._raw_position = 0

    @property
    def position_percent(self) -> float:
        return self._position_percent

    @property
    def raw_position(self) -> int:
        return self._raw_position

    def _set_connected(self, connected: bool) -> None:
        if self._connected != connected:
            self._connected = connected
            self.connection_changed.emit(connected)

    def _raw_to_percent(self, raw_value: int) -> float:
        low = self.min_raw
        high = self.max_raw
        if high == low:
            return 0.0

        if low > high:
            low, high = high, low

        clipped = max(low, min(high, raw_value))
        percent = ((clipped - high) / (low - high)) * 100.0
        if self.invert_axis:
            percent = 100.0 - percent
        return max(0.0, min(100.0, percent))

    def _smooth_position(self, new_position: float) -> float:
        if not self._has_filtered_position:
            self._filtered_position_percent = new_position
            self._has_filtered_position = True
            return new_position

        delta = new_position - self._filtered_position_percent
        if abs(delta) <= self.deadband_percent:
            return self._filtered_position_percent

        self._filtered_position_percent += delta * self.smoothing_alpha
        return self._filtered_position_percent

    def _emit_axis_value(self, raw_value: int) -> None:
        self._raw_position = int(raw_value)
        self.raw_position_changed.emit(self._raw_position)

        new_position = self._raw_to_percent(raw_value)
        smoothed_position = self._smooth_position(new_position)
        if abs(smoothed_position - self._position_percent) >= 0.05:
            self._position_percent = smoothed_position
            self.position_changed.emit(smoothed_position)


class LinuxJoystickReader(QObject, _JoystickConfigMixin):
    """Leitura via /dev/input/js* (WSL ou Linux nativo)."""

    position_changed = Signal(float)
    raw_position_changed = Signal(int)
    connection_changed = Signal(bool)
    error_occurred = Signal(str)

    def __init__(self) -> None:
        super().__init__()
        self.device_path = os.getenv("SCCA_JOYSTICK_DEVICE", "/dev/input/js0")
        self._load_config()
        axis_env = os.getenv("SCCA_JOYSTICK_AXIS", "")
        if not axis_env.strip().isdigit():
            self.axis_number = None
        self._fd: int | None = None
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._poll_device)

    def start(self, poll_interval_ms: int = 20) -> None:
        self._timer.start(max(10, poll_interval_ms))
        self._poll_device()

    def stop(self) -> None:
        self._timer.stop()
        self._close_device()

    def _close_device(self) -> None:
        if self._fd is not None:
            try:
                os.close(self._fd)
            except OSError:
                pass
            self._fd = None
        self._set_connected(False)

    def _open_device(self) -> bool:
        if self._fd is not None:
            return True

        try:
            open_flags = os.O_RDONLY
            if hasattr(os, "O_NONBLOCK"):
                open_flags |= os.O_NONBLOCK
            self._fd = os.open(self.device_path, open_flags)
            self._set_connected(True)
            return True
        except FileNotFoundError:
            self._set_connected(False)
            return False
        except PermissionError as exc:
            self._close_device()
            self.error_occurred.emit(f"Sem permissão para ler {self.device_path}: {exc}")
            return False
        except OSError as exc:
            self._close_device()
            self.error_occurred.emit(f"Falha ao abrir {self.device_path}: {exc}")
            return False

    def _poll_device(self) -> None:
        if not self._open_device() or self._fd is None:
            return

        while True:
            try:
                chunk = os.read(self._fd, 8)
            except BlockingIOError:
                break
            except OSError as exc:
                self.error_occurred.emit(f"Erro ao ler joystick em {self.device_path}: {exc}")
                self._close_device()
                return

            if len(chunk) < 8:
                break

            _event_time, value, event_type, number = struct.unpack("<IhBB", chunk)
            event_kind = event_type & 0x7F
            if event_kind != 0x02:
                continue

            if self.axis_number is None:
                self.axis_number = number

            if number != self.axis_number:
                continue

            self._emit_axis_value(int(value))


class WindowsJoystickReader(QObject, _JoystickConfigMixin):
    """Leitura via winmm (Arduino Leonardo e outros HID no Windows)."""

    position_changed = Signal(float)
    raw_position_changed = Signal(int)
    connection_changed = Signal(bool)
    error_occurred = Signal(str)

    _AXIS_FLAGS = (0x001, 0x002, 0x004, 0x008, 0x010, 0x020)
    _AXIS_FIELDS = ("dwXpos", "dwYpos", "dwZpos", "dwRpos", "dwUpos", "dwVpos")

    def __init__(self) -> None:
        super().__init__()
        import ctypes

        self._ctypes = ctypes
        self._winmm = ctypes.windll.winmm
        self.joystick_id = int(os.getenv("SCCA_JOYSTICK_ID", "0"))
        self._load_config()
        self.device_path = f"Windows Joystick {self.joystick_id}"
        self._warned_missing = False
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._poll_device)
        self._refresh_device_name()

    def _refresh_device_name(self) -> None:
        try:
            from ctypes import wintypes

            ctypes = self._ctypes

            class JOYCAPSW(ctypes.Structure):
                _fields_ = [
                    ("wMid", wintypes.WORD),
                    ("wPid", wintypes.WORD),
                    ("szPname", wintypes.WCHAR * 32),
                    ("wXmin", wintypes.UINT),
                    ("wXmax", wintypes.UINT),
                    ("wYmin", wintypes.UINT),
                    ("wYmax", wintypes.UINT),
                    ("wZmin", wintypes.UINT),
                    ("wZmax", wintypes.UINT),
                    ("wNumButtons", wintypes.UINT),
                    ("wPeriodMin", wintypes.UINT),
                    ("wPeriodMax", wintypes.UINT),
                    ("wRmin", wintypes.UINT),
                    ("wRmax", wintypes.UINT),
                    ("wUmin", wintypes.UINT),
                    ("wUmax", wintypes.UINT),
                    ("wVmin", wintypes.UINT),
                    ("wVmax", wintypes.UINT),
                    ("wCaps", wintypes.UINT),
                    ("wMaxAxes", wintypes.UINT),
                    ("wNumAxes", wintypes.UINT),
                    ("wMaxButtons", wintypes.UINT),
                    ("szRegKey", wintypes.WCHAR * 32),
                    ("szOEMVxD", wintypes.WCHAR * 260),
                ]

            caps = JOYCAPSW()
            if self._winmm.joyGetDevCapsW(self.joystick_id, self._ctypes.byref(caps), self._ctypes.sizeof(caps)) == 0:
                name = caps.szPname.strip() or f"Joystick {self.joystick_id}"
                self.device_path = name
        except Exception:
            pass

    def start(self, poll_interval_ms: int = 20) -> None:
        self._timer.start(max(10, poll_interval_ms))
        self._poll_device()

    def stop(self) -> None:
        self._timer.stop()
        self._set_connected(False)

    @staticmethod
    def _winmm_to_linux_raw(value: int) -> int:
        # winmm usa 0..65535 com centro ~32768; Linux/js usa ~-32767..32767
        return int(value) - 32768

    def _poll_device(self) -> None:
        from ctypes import wintypes

        ctypes = self._ctypes

        class JOYINFOEX(ctypes.Structure):
            _fields_ = [
                ("dwSize", wintypes.DWORD),
                ("dwFlags", wintypes.DWORD),
                ("dwXpos", wintypes.DWORD),
                ("dwYpos", wintypes.DWORD),
                ("dwZpos", wintypes.DWORD),
                ("dwRpos", wintypes.DWORD),
                ("dwUpos", wintypes.DWORD),
                ("dwVpos", wintypes.DWORD),
                ("dwButtons", wintypes.DWORD),
                ("dwButtonNumber", wintypes.DWORD),
                ("dwPOV", wintypes.DWORD),
                ("dwReserved1", wintypes.DWORD),
                ("dwReserved2", wintypes.DWORD),
            ]

        axis = max(0, min(5, int(self.axis_number)))
        info = JOYINFOEX()
        info.dwSize = self._ctypes.sizeof(JOYINFOEX)
        info.dwFlags = self._AXIS_FLAGS[axis]

        result = self._winmm.joyGetPosEx(self.joystick_id, self._ctypes.byref(info))
        if result != 0:
            self._set_connected(False)
            if not self._warned_missing:
                self._warned_missing = True
                self.error_occurred.emit(
                    f"Joystick {self.joystick_id} não encontrado no Windows "
                    f"(joyGetPosEx={result}). Verifique USB e SCCA_JOYSTICK_ID."
                )
            return

        if not self._connected:
            self._refresh_device_name()
        self._set_connected(True)
        win_value = int(getattr(info, self._AXIS_FIELDS[axis]))
        self._emit_axis_value(self._winmm_to_linux_raw(win_value))


def create_joystick_reader() -> LinuxJoystickReader | WindowsJoystickReader:
    if sys.platform == "win32":
        return WindowsJoystickReader()
    return LinuxJoystickReader()
