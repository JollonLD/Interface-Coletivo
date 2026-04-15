from __future__ import annotations

import math
import time
from dataclasses import dataclass, asdict

from PySide6.QtCore import QThread, Signal


@dataclass
class TelemetryPacket:
    position_percent: float
    trim_hold: bool
    beep_trim: str
    pa_active: bool
    hydraulic_failure: bool
    pilot_force_kg: float
    udp_connected: bool
    usb_connected: bool
    selected_maneuver: str
    maneuver_active: bool
    maneuver_state: str
    timestamp: float


class BaseDataReceiver:
    """Contract for data acquisition. Replace with real UDP/USB implementation later."""

    def receive_data(self) -> TelemetryPacket:
        raise NotImplementedError


class MockDataReceiver(BaseDataReceiver):
    """Mock source that emulates real-time telemetry from UDP + USB."""

    def __init__(self) -> None:
        self.position_percent = 0.0
        self._previous_position = self.position_percent
        self.trim_hold = True
        self.beep_trim = "NEUTRAL"
        self.pa_active = True
        self.hydraulic_failure = False
        self.pilot_force_kg = 0.0
        self.udp_connected = True
        self.usb_connected = True
        self.random_motion = False
        self.maneuver_time_s = 0.0
        self.sample_time_s = 0.1
        self.selected_maneuver = "Circuito Classico"
        self.maneuver_state = "IDLE"
        self.trim_reference_percent = 0.0
        self.hold_trim_reference = False
        self.force_idle_kg = 0.0
        self.force_gain_kg_per_pct_s = 0.22
        self.force_max_kg = 4.0
        self.force_rate_deadband_pct_s = 0.05
        self._maneuver_catalog = [
            "Circuito Classico",
            "Subida Rapida e Arremetida",
            "Autorrotacao Treino",
        ]

    def _ramp(self, current: float, target: float, start: float, end: float) -> float:
        if end <= start:
            return target
        ratio = (current - start) / (end - start)
        ratio = max(0.0, min(1.0, ratio))
        return target[0] + (target[1] - target[0]) * ratio

    def _maneuver_collective_profile(self, t: float) -> float:
        """Classical rotorcraft maneuver profile in collective percentage."""
        cycle = 62.0
        tm = t % cycle

        # 0-8 s: takeoff and initial climb (5% -> 38%)
        if tm < 8.0:
            return self._ramp(tm, (5.0, 38.0), 0.0, 8.0)

        # 8-16 s: continued climb to operational altitude (38% -> 62%)
        if tm < 16.0:
            return self._ramp(tm, (38.0, 62.0), 8.0, 16.0)

        # 16-26 s: coordinated turn at quasi-constant altitude
        if tm < 26.0:
            return 62.0 + 3.0 * math.sin((tm - 16.0) * 0.9)

        # 26-36 s: descent to approach (62% -> 35%)
        if tm < 36.0:
            return self._ramp(tm, (62.0, 35.0), 26.0, 36.0)

        # 36-46 s: flare and hover correction around low altitude
        if tm < 46.0:
            return 35.0 + 7.0 * math.sin((tm - 36.0) * 0.8)

        # 46-58 s: stabilized landing (30% -> 8%)
        if tm < 58.0:
            return self._ramp(tm, (30.0, 8.0), 46.0, 58.0)

        # 58-62 s: ground idle
        return 8.0

    def _maneuver_rapid_climb_go_around(self, t: float) -> float:
        cycle = 42.0
        tm = t % cycle

        # 0-6 s: aggressive climb command
        if tm < 6.0:
            return self._ramp(tm, (12.0, 74.0), 0.0, 6.0)

        # 6-13 s: hold high collective
        if tm < 13.0:
            return 74.0 + 2.0 * math.sin((tm - 6.0) * 1.2)

        # 13-22 s: reduce for downwind/approach
        if tm < 22.0:
            return self._ramp(tm, (74.0, 34.0), 13.0, 22.0)

        # 22-28 s: go-around command
        if tm < 28.0:
            return self._ramp(tm, (34.0, 68.0), 22.0, 28.0)

        # 28-37 s: stabilized transition
        if tm < 37.0:
            return 58.0 + 5.0 * math.sin((tm - 28.0) * 0.9)

        # 37-42 s: return to low-power segment
        return self._ramp(tm, (50.0, 12.0), 37.0, 42.0)

    def _maneuver_autorotation_training(self, t: float) -> float:
        cycle = 46.0
        tm = t % cycle

        # 0-8 s: climb setup
        if tm < 8.0:
            return self._ramp(tm, (20.0, 58.0), 0.0, 8.0)

        # 8-11 s: entry to autorotation (rapid collective reduction)
        if tm < 11.0:
            return self._ramp(tm, (58.0, 12.0), 8.0, 11.0)

        # 11-24 s: steady descent in autorotation band
        if tm < 24.0:
            return 14.0 + 2.5 * math.sin((tm - 11.0) * 1.1)

        # 24-29 s: flare and recovery pull
        if tm < 29.0:
            return self._ramp(tm, (16.0, 52.0), 24.0, 29.0)

        # 29-36 s: cushion and touchdown
        if tm < 36.0:
            return self._ramp(tm, (52.0, 10.0), 29.0, 36.0)

        # 36-46 s: idle + reset
        return 10.0 + 1.0 * math.sin((tm - 36.0) * 0.8)

    def list_maneuvers(self) -> list[str]:
        return list(self._maneuver_catalog)

    def _get_selected_duration(self) -> float:
        if self.selected_maneuver == "Subida Rapida e Arremetida":
            return 42.0
        if self.selected_maneuver == "Autorrotacao Treino":
            return 46.0
        return 62.0

    def set_selected_maneuver(self, name: str) -> None:
        if name in self._maneuver_catalog:
            self.selected_maneuver = name
            self.maneuver_time_s = 0.0
            if not self.random_motion:
                self.maneuver_state = "IDLE"

    def set_maneuver_active(self, enabled: bool) -> None:
        if enabled:
            if not self.pa_active:
                self.random_motion = False
                self.maneuver_state = "IDLE"
                return

            self.random_motion = True
            self.maneuver_time_s = 0.0
            self.trim_reference_percent = self.position_percent
            self.hold_trim_reference = False
            self.maneuver_state = "RUNNING"
        else:
            self.random_motion = False
            if self.maneuver_state == "RUNNING":
                self.maneuver_state = "IDLE"

    def start_maneuver(self, name: str) -> None:
        self.set_selected_maneuver(name)
        self.set_maneuver_active(True)

    def stop_maneuver(self, state: str = "IDLE") -> None:
        self.random_motion = False
        self.maneuver_state = state

    def is_maneuver_active(self) -> bool:
        return self.random_motion

    def set_pa_active(self, active: bool) -> None:
        # User decoupling PA aborts current maneuver and returns to initial trim reference.
        if not active and self.random_motion:
            self.stop_maneuver("ABORTED")
            self.position_percent = self.trim_reference_percent
            self.hold_trim_reference = True
        self.pa_active = active

    def set_hydraulic_failure(self, enabled: bool) -> None:
        self.hydraulic_failure = enabled

    def set_random_motion(self, enabled: bool) -> None:
        # Backward compatibility with previous dashboard control.
        self.set_maneuver_active(enabled)

    def _active_maneuver_profile(self, t: float) -> float:
        if self.selected_maneuver == "Subida Rapida e Arremetida":
            return self._maneuver_rapid_climb_go_around(t)
        if self.selected_maneuver == "Autorrotacao Treino":
            return self._maneuver_autorotation_training(t)
        return self._maneuver_collective_profile(t)

    def _simulate_motion(self) -> None:
        self._previous_position = self.position_percent

        if self.hydraulic_failure:
            if self.random_motion:
                self.stop_maneuver("ABORTED")
            self.hold_trim_reference = False
            self.position_percent = 0.0
            return

        if self.random_motion:
            duration = self._get_selected_duration()
            if self.maneuver_time_s >= duration:
                self.stop_maneuver("COMPLETED")
                self.position_percent = self.trim_reference_percent
                self.hold_trim_reference = True
            else:
                self.position_percent = self._active_maneuver_profile(self.maneuver_time_s)
                self.maneuver_time_s += self.sample_time_s
        else:
            if self.hold_trim_reference:
                self.position_percent = self.trim_reference_percent
            else:
                # Hover-like idle behavior when maneuver routine is disabled.
                self.position_percent = self.position_percent

        self.position_percent = max(0.0, min(100.0, self.position_percent))

    def _simulate_states(self) -> None:
        tm = self.maneuver_time_s
        slope = self.position_percent - self._previous_position
        if slope > 0.15:
            self.beep_trim = "UP"
        elif slope < -0.15:
            self.beep_trim = "DOWN"
        else:
            self.beep_trim = "NEUTRAL"

        # Trim remains released during aggressive transitions and hold during stable phases.
        self.trim_hold = abs(slope) < 0.22

        # If PA is not active, running maneuver is aborted for safety behavior.
        if self.random_motion and not self.pa_active:
            self.stop_maneuver("ABORTED")
            self.position_percent = self.trim_reference_percent
            self.hold_trim_reference = True

    def _simulate_connectivity(self) -> None:
        if not self.random_motion:
            self.udp_connected = True
            self.usb_connected = True
            return

        duration = self._get_selected_duration()
        progress = self.maneuver_time_s / max(duration, 1.0)
        self.udp_connected = not (0.65 <= progress < 0.68)
        self.usb_connected = not (0.71 <= progress < 0.75)

    def _simulate_force(self) -> None:
        if self.hydraulic_failure:
            self.pilot_force_kg = self.force_max_kg
            return

        # Force is proportional to the absolute time derivative of the collective command.
        dpos = self.position_percent - self._previous_position
        rate_pct_s = abs(dpos / max(self.sample_time_s, 1e-6))

        # Idle/steady handle: no meaningful command movement means zero force.
        if rate_pct_s < self.force_rate_deadband_pct_s:
            self.pilot_force_kg = 0.0
            return

        oscillation = 0.06 * math.sin(self.maneuver_time_s * 1.4)
        force_from_derivative = self.force_gain_kg_per_pct_s * rate_pct_s
        raw_force = self.force_idle_kg + force_from_derivative + oscillation
        self.pilot_force_kg = max(0.0, min(self.force_max_kg, raw_force))

    def receive_data(self) -> TelemetryPacket:
        # Single acquisition point for easy replacement by UDP socket + serial reads.
        if self.random_motion and not self.pa_active:
            self.stop_maneuver("ABORTED")
            self.position_percent = self.trim_reference_percent
            self.hold_trim_reference = True

        self._simulate_motion()
        self._simulate_states()
        self._simulate_connectivity()
        self._simulate_force()

        return TelemetryPacket(
            position_percent=self.position_percent,
            trim_hold=self.trim_hold,
            beep_trim=self.beep_trim,
            pa_active=self.pa_active,
            hydraulic_failure=self.hydraulic_failure,
            pilot_force_kg=self.pilot_force_kg,
            udp_connected=self.udp_connected,
            usb_connected=self.usb_connected,
            selected_maneuver=self.selected_maneuver,
            maneuver_active=self.random_motion,
            maneuver_state=self.maneuver_state,
            timestamp=time.time(),
        )


class DataWorkerThread(QThread):
    packet_received = Signal(dict)

    def __init__(self, receiver: BaseDataReceiver, interval_ms: int = 100) -> None:
        super().__init__()
        self.receiver = receiver
        self.interval_ms = interval_ms
        self._running = True

    def run(self) -> None:
        while self._running:
            packet = self.receiver.receive_data()
            self.packet_received.emit(asdict(packet))
            self.msleep(self.interval_ms)

    def stop(self) -> None:
        self._running = False
