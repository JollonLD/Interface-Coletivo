from __future__ import annotations

import json
import logging
import math
import os
import time

from PySide6.QtCore import QTimer, Qt
from PySide6.QtGui import QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import (
    QAbstractButton,
    QApplication,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPushButton,
    QProgressBar,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

from scca.styles import DASHBOARD_QSS
from scca.udp_receiver import UDPReceiver, CommandSender, MockRaspberryAutopilot


class ToggleSliderButton(QAbstractButton):
    def __init__(self, text: str, danger: bool = False, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setText(text)
        self.setCheckable(True)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setMinimumSize(270, 42)
        self.danger = danger

    def paintEvent(self, event) -> None:
        del event
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        frame_rect = self.rect().adjusted(1, 1, -1, -1)
        painter.setPen(QPen(QColor("#304156"), 1.2))
        painter.setBrush(QColor("#13202d"))
        painter.drawRoundedRect(frame_rect, 9, 9)

        painter.setPen(QColor("#dbe5ef"))
        painter.setFont(QFont("Rajdhani", 12, QFont.Weight.Bold))
        text_rect = self.rect().adjusted(12, 0, -92, 0)
        painter.drawText(text_rect, Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft, self.text())

        track_w = 52
        track_h = 26
        track_x = self.width() - track_w - 14
        track_y = (self.height() - track_h) // 2

        if self.isChecked():
            track_color = QColor("#f04a54") if self.danger else QColor("#16ff9a")
            border_color = QColor("#b83a44") if self.danger else QColor("#1e8f62")
            knob_x = track_x + track_w - 22 - 2
        else:
            track_color = QColor("#334354")
            border_color = QColor("#3b4f63")
            knob_x = track_x + 2

        painter.setPen(QPen(border_color, 1.1))
        painter.setBrush(track_color)
        painter.drawRoundedRect(track_x, track_y, track_w, track_h, 13, 13)

        painter.setPen(QPen(QColor("#0f141b"), 1.0))
        painter.setBrush(QColor("#ecf3fb"))
        painter.drawEllipse(knob_x, track_y + 2, 22, 22)

        if self.underMouse():
            painter.setPen(QPen(QColor("#4e6a85"), 1.2))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawRoundedRect(frame_rect, 9, 9)


class LedIndicator(QWidget):
    def __init__(self, color_on: str, color_off: str = "#3b4754", parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setFixedSize(18, 18)
        self.color_on = QColor(color_on)
        self.color_off = QColor(color_off)
        self.is_on = False

    def set_on(self, enabled: bool) -> None:
        self.is_on = enabled
        self.update()

    def paintEvent(self, event) -> None:
        del event
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        color = self.color_on if self.is_on else self.color_off
        painter.setPen(QPen(QColor("#0f141b"), 1.5))
        painter.setBrush(color)
        painter.drawEllipse(1, 1, 16, 16)


class CircularForceGauge(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setMinimumSize(230, 230)
        self.kg_value = 0.0
        self.max_kg = 80.0

    def set_force_kg(self, value: float) -> None:
        self.kg_value = max(0.0, min(self.max_kg, value))
        self.update()

    def paintEvent(self, event) -> None:
        del event
        width = self.width()
        height = self.height()
        side = min(width, height) - 14
        rect_x = (width - side) // 2
        rect_y = (height - side) // 2

        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        painter.setPen(QPen(QColor("#243548"), 14, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawArc(rect_x, rect_y, side, side, 225 * 16, -270 * 16)

        span = int((self.kg_value / self.max_kg) * 270)
        painter.setPen(QPen(QColor("#f58f2d"), 14, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawArc(rect_x, rect_y, side, side, 225 * 16, -span * 16)

        painter.setPen(QColor("#eaf3ff"))
        value_font = QFont("Rajdhani", 26, QFont.Weight.Bold)
        painter.setFont(value_font)
        painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, f"{self.kg_value:04.1f} KG")

        newtons = self.kg_value * 9.80665
        sub_rect = self.rect().adjusted(0, 48, 0, 0)
        painter.setPen(QColor("#82d8ff"))
        unit_font = QFont("Rajdhani", 13, QFont.Weight.DemiBold)
        painter.setFont(unit_font)
        painter.drawText(sub_rect, Qt.AlignmentFlag.AlignCenter, f"{newtons:05.1f} N")

        painter.setPen(QColor("#7f93a8"))
        tick_font = QFont("Rajdhani", 10, QFont.Weight.Medium)
        painter.setFont(tick_font)
        for pct, label in [(0.0, "0"), (0.25, "20"), (0.5, "40"), (0.75, "60"), (1.0, "80")]:
            angle_deg = 225 - (270 * pct)
            rad = math.radians(angle_deg)
            r = (side / 2) - 4
            tx = width / 2 + (r - 15) * math.cos(rad)
            ty = height / 2 - (r - 15) * math.sin(rad)
            painter.drawText(int(tx) - 10, int(ty) + 5, label)


class SccaDashboard(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Dashboard do Sistema de Comando Coletivo")
        self.resize(1300, 760)

        # Inicializar UDP Receiver (recebe telemetria do Raspberry Pi)
        self.udp_receiver = UDPReceiver(host="0.0.0.0", port=12345)
        self.udp_receiver.packet_received.connect(self._on_udp_packet_received)
        self.udp_receiver.error_occurred.connect(self._on_udp_error)
        self.udp_receiver.connection_status_changed.connect(self._on_udp_connection_changed)

        # Inicializar Command Sender (envia manobras para Raspberry Pi)
        initial_host = os.getenv("SCCA_COMMAND_HOST", "127.0.0.1")
        try:
            initial_port = int(os.getenv("SCCA_COMMAND_PORT", "12346"))
        except ValueError:
            initial_port = 12346
            self.logger = logging.getLogger("Dashboard")
            self.logger.warning("SCCA_COMMAND_PORT inválida; usando 12346")
        self.command_sender = CommandSender(receiver_host=initial_host, receiver_port=initial_port)
        self.command_sender.command_sent.connect(self._on_command_sent)
        self.command_sender.error_occurred.connect(self._on_command_error)
        self._command_target_host = initial_host
        self._command_target_port = initial_port

        # Simulador local opcional de Raspberry (comando + telemetria)
        self.mock_raspberry = MockRaspberryAutopilot(
            command_host="127.0.0.1",
            command_port=initial_port,
            telemetry_host="127.0.0.1",
            telemetry_port=12345,
        )
        self.mock_raspberry.status_changed.connect(self._on_mock_status_changed)
        self.mock_raspberry.error_occurred.connect(self._on_mock_error)

        root = QWidget()
        self.setCentralWidget(root)
        main_layout = QVBoxLayout(root)
        main_layout.setSpacing(12)

        title = QLabel("DASHBOARD")
        title.setObjectName("title")
        subtitle = QLabel("Sistema de Comando Coletivo | Supervisao Integrada")
        subtitle.setObjectName("subtitle")
        main_layout.addWidget(title)
        main_layout.addWidget(subtitle)

        body = QHBoxLayout()
        body.setSpacing(12)
        main_layout.addLayout(body, 1)

        position_panel = self._build_position_panel()
        states_panel = self._build_states_panel()
        telemetry_panel = self._build_telemetry_panel()
        maneuver_panel = self._build_maneuver_panel()
        udp_panel = self._build_udp_panel()

        center_container = QWidget()
        center_layout = QVBoxLayout(center_container)
        center_layout.setContentsMargins(0, 0, 0, 0)
        center_layout.setSpacing(12)
        center_layout.addWidget(telemetry_panel, 1)
        center_layout.addWidget(maneuver_panel)
        center_layout.addWidget(udp_panel)

        body.addWidget(position_panel)
        body.addWidget(center_container, 1)
        body.addWidget(states_panel)

        self.flash_timer = QTimer(self)
        self.flash_timer.setInterval(300)
        self.flash_timer.timeout.connect(self._toggle_alert_flash)
        self._flash_on = False
        self._last_udp_packet_time = 0.0
        
        # Logger
        self.logger = logging.getLogger("Dashboard")

        # Iniciar servidor UDP
        self.udp_receiver.start()

    def _panel_frame(self) -> QFrame:
        frame = QFrame()
        frame.setObjectName("panel")
        frame.setFrameShape(QFrame.Shape.NoFrame)
        return frame

    def _build_position_panel(self) -> QFrame:
        panel = self._panel_frame()
        panel.setProperty("sidebar", "true")
        panel.setFixedWidth(260)
        layout = QVBoxLayout(panel)

        head = QLabel("Monitoramento de Posicao")
        head.setObjectName("subtitle")
        layout.addWidget(head)

        row = QHBoxLayout()
        self.position_bar = QProgressBar()
        self.position_bar.setObjectName("verticalGauge")
        self.position_bar.setOrientation(Qt.Orientation.Vertical)
        self.position_bar.setRange(0, 1000)
        self.position_bar.setValue(523)
        self.position_bar.setFixedWidth(64)
        self.position_bar.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Expanding)
        self.position_bar.setTextVisible(False)

        self.position_display = QLabel("52.3%")
        self.position_display.setObjectName("displayValue")

        row.addWidget(self.position_bar)
        row.addWidget(self.position_display, alignment=Qt.AlignmentFlag.AlignCenter)
        layout.addLayout(row, 1)
        return panel

    def _state_label(self, text: str, state: str = "off") -> QLabel:
        lbl = QLabel(text)
        lbl.setObjectName("stateLabel")
        lbl.setProperty("state", state)
        return lbl

    def _build_states_panel(self) -> QFrame:
        panel = self._panel_frame()
        layout = QVBoxLayout(panel)

        head = QLabel("Estados do Sistema")
        head.setObjectName("subtitle")
        layout.addWidget(head)

        self.trim_hold_lbl = self._state_label("Trim: HOLD", "ok")
        self.trim_release_lbl = self._state_label("Trim: RELEASE", "off")

        self.beep_up_lbl = self._state_label("Beep Trim: UP", "off")
        self.beep_down_lbl = self._state_label("Beep Trim: DOWN", "off")

        self.pa_active_lbl = self._state_label("PA: ACTIVE", "ok")
        self.pa_override_lbl = self._state_label("PA: OVERRIDE", "off")

        self.alert_lbl = QLabel("ALERTA CRITICO: FALHA HIDRAULICA")
        self.alert_lbl.setObjectName("criticalAlert")
        self.alert_lbl.hide()

        layout.addWidget(self.trim_hold_lbl)
        layout.addWidget(self.trim_release_lbl)
        layout.addSpacing(8)
        layout.addWidget(self.beep_up_lbl)
        layout.addWidget(self.beep_down_lbl)
        layout.addSpacing(8)
        layout.addWidget(self.pa_active_lbl)
        layout.addWidget(self.pa_override_lbl)
        layout.addSpacing(10)
        layout.addWidget(self.alert_lbl)
        layout.addStretch(1)
        return panel

    def _build_telemetry_panel(self) -> QFrame:
        panel = self._panel_frame()
        layout = QVBoxLayout(panel)

        head = QLabel("Telemetria em Tempo Real")
        head.setObjectName("subtitleTelemetry")
        layout.addWidget(head)

        self.force_gauge = CircularForceGauge()
        self.force_gauge.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        layout.addWidget(self.force_gauge, alignment=Qt.AlignmentFlag.AlignCenter)

        conn_row = QHBoxLayout()
        udp_label = QLabel("UDP (Raspberry Pi)")
        udp_label.setObjectName("subtitle")
        self.udp_led = LedIndicator("#16ff9a")

        usb_label = QLabel("USB (Arduino)")
        usb_label.setObjectName("subtitle")
        self.usb_led = LedIndicator("#16ff9a")

        conn_row.addWidget(udp_label)
        conn_row.addWidget(self.udp_led)
        conn_row.addSpacing(18)
        conn_row.addWidget(usb_label)
        conn_row.addWidget(self.usb_led)
        conn_row.addStretch(1)

        layout.addLayout(conn_row)
        return panel

    # def _build_tests_panel(self, states_panel: QFrame) -> QFrame:
    #     panel = self._panel_frame()
    #     panel.setProperty("sidebar", "true")
    #     panel.setFixedWidth(300)
    #     layout = QVBoxLayout(panel)

    #     head = QLabel("Painel de Testes")
    #     head.setObjectName("subtitle")
    #     layout.addWidget(head)

    #     self.toggle_pa = ToggleSliderButton("PA Acoplado")
    #     self.toggle_pa.setChecked(self.receiver.pa_active)
    #     self.toggle_pa.setMinimumSize(260, 54)

    #     self.toggle_pa.toggled.connect(self._set_pa_active)

    #     layout.addWidget(self.toggle_pa)
    #     layout.addSpacing(8)
    #     layout.addWidget(states_panel, 1)
    #     layout.addStretch(1)
    #     return panel

    def _build_maneuver_panel(self) -> QFrame:
        panel = self._panel_frame()
        layout = QVBoxLayout(panel)

        head = QLabel("Painel de Manobras (Autopiloto)")
        head.setObjectName("subtitle")
        layout.addWidget(head)

        matrix = QGridLayout()
        matrix.setSpacing(10)

        self.maneuver_buttons: dict[str, QPushButton] = {}
        # Lista de manobras disponíveis
        maneuvers = ["Manobra 1", "Manobra 2", "Manobra 3", "Manobra 4"]
        
        for idx, name in enumerate(maneuvers):
            btn = QPushButton(name)
            btn.setObjectName("matrixTile")
            btn.setProperty("tileKind", "maneuver")
            btn.setProperty("runState", "idle")
            btn.setCheckable(True)
            btn.setMinimumSize(170, 96)
            btn.toggled.connect(lambda checked, mn=name: self._toggle_maneuver_command(mn, checked))
            self.maneuver_buttons[name] = btn
            matrix.addWidget(btn, idx // 2, idx % 2)

        self.pane_tile = QPushButton("Falha Hidraulica")
        self.pane_tile.setObjectName("matrixTile")
        self.pane_tile.setProperty("tileKind", "pane")
        self.pane_tile.setProperty("runState", "idle")
        self.pane_tile.setCheckable(True)
        self.pane_tile.setMinimumSize(170, 96)
        self.pane_tile.toggled.connect(self._send_hydraulic_failure_command)
        matrix.addWidget(self.pane_tile, 1, 1)

        self.maneuver_hint = QLabel("Clique em uma manobra para enviar comando ao Raspberry Pi")
        self.maneuver_hint.setObjectName("subtitle")

        layout.addLayout(matrix)
        layout.addWidget(self.maneuver_hint)
        layout.addStretch(1)
        return panel

    def _build_udp_panel(self) -> QFrame:
        """Painel para monitoramento de dados UDP do Raspberry Pi."""
        panel = self._panel_frame()
        layout = QVBoxLayout(panel)

        head = QLabel("Status UDP - Telemetria do Raspberry Pi")
        head.setObjectName("subtitle")
        layout.addWidget(head)

        # Status de conexão
        conn_row = QHBoxLayout()
        conn_label = QLabel("Status UDP:")
        conn_label.setObjectName("subtitle")
        self.udp_status_led = LedIndicator("#16ff9a")
        conn_row.addWidget(conn_label)
        conn_row.addWidget(self.udp_status_led)
        conn_row.addStretch(1)
        layout.addLayout(conn_row)

        # Display dos dados recebidos
        self.udp_data_info = QLabel("Aguardando dados UDP...")
        self.udp_data_info.setObjectName("subtitle")
        self.udp_data_info.setStyleSheet("color: #82d8ff; font-size: 11px;")
        layout.addWidget(self.udp_data_info)

        self.udp_endpoints_info = QLabel(
            f"Escutando telemetria em 0.0.0.0:12345 | Enviando comandos para {self._command_target_host}:{self._command_target_port}"
        )
        self.udp_endpoints_info.setObjectName("subtitle")
        self.udp_endpoints_info.setStyleSheet("color: #9db3c9; font-size: 10px;")
        layout.addWidget(self.udp_endpoints_info)

        # Contador de pacotes
        self.udp_packet_count = QLabel("Pacotes recebidos: 0")
        self.udp_packet_count.setObjectName("subtitle")
        self.udp_packet_count.setStyleSheet("color: #7f93a8; font-size: 10px;")
        layout.addWidget(self.udp_packet_count)

        # Display do último pacote
        self.udp_last_packet = QLabel("[Últimos dados do sensor]")
        self.udp_last_packet.setObjectName("subtitle")
        self.udp_last_packet.setStyleSheet(
            "color: #16ff9a; font-size: 10px; font-family: 'Courier New'; "
            "background-color: rgba(10, 20, 30, 150); padding: 6px; border-radius: 4px;"
        )
        self.udp_last_packet.setWordWrap(True)
        layout.addWidget(self.udp_last_packet)

        self.toggle_mock_mode = ToggleSliderButton("Simular Raspberry (Local)")
        self.toggle_mock_mode.setMinimumSize(250, 42)
        self.toggle_mock_mode.toggled.connect(self._toggle_mock_mode)
        layout.addWidget(self.toggle_mock_mode)

        layout.addStretch(1)
        return panel

    def _refresh_tile_style(self, tile: QPushButton) -> None:
        tile.style().unpolish(tile)
        tile.style().polish(tile)
        tile.update()

    def _toggle_maneuver_command(self, maneuver_name: str, enabled: bool) -> None:
        """Liga/desliga execução de manobra no Raspberry Pi."""
        if enabled:
            self.logger.info(f"Enviando comando de manobra: {maneuver_name}")
            self.command_sender.send_maneuver_command(maneuver_name, action="start")
            self.maneuver_hint.setText(f"Comando enviado: {maneuver_name} | Executando...")
        else:
            self.logger.info(f"Cancelando manobra: {maneuver_name}")
            self.command_sender.send_maneuver_stop(maneuver_name)
            self.maneuver_hint.setText(f"Comando enviado: {maneuver_name} | Cancelada")

    def _send_hydraulic_failure_command(self, enabled: bool) -> None:
        """Envia comando de simulação de falha hidráulica."""
        status = "ativado" if enabled else "desativado"
        self.logger.info(f"Enviando comando: Pane hidráulica {status}")
        self.command_sender.send_system_command("set_hydraulic_failure", enabled)
        self.maneuver_hint.setText(f"Pane hidráulica {status}")

    def _toggle_mock_mode(self, enabled: bool) -> None:
        if enabled:
            ok = self.mock_raspberry.start(interval_ms=100)
            if ok:
                self.maneuver_hint.setText("Mock Raspberry ativo | Comandos locais habilitados")
            else:
                self.toggle_mock_mode.blockSignals(True)
                self.toggle_mock_mode.setChecked(False)
                self.toggle_mock_mode.blockSignals(False)
        else:
            self.mock_raspberry.stop()
            self.maneuver_hint.setText("Mock Raspberry desativado")

    def _on_mock_status_changed(self, running: bool) -> None:
        if running:
            self.udp_data_info.setText("Mock Raspberry ativo - enviando telemetria")
        else:
            self.udp_data_info.setText("Mock Raspberry parado")

    def _on_mock_error(self, error_msg: str) -> None:
        self.logger.error(f"Mock Raspberry error: {error_msg}")
        self.udp_data_info.setText(f"ERRO MOCK: {error_msg}")

    def _set_state(self, label: QLabel, state: str) -> None:
        label.setProperty("state", state)
        label.style().unpolish(label)
        label.style().polish(label)
        label.update()

    def _toggle_alert_flash(self) -> None:
        self._flash_on = not self._flash_on
        self.alert_lbl.setProperty("flash", "true" if self._flash_on else "false")
        self.alert_lbl.style().unpolish(self.alert_lbl)
        self.alert_lbl.style().polish(self.alert_lbl)

    def _extract_telemetry(self, packet_dict: dict) -> dict:
        """Normaliza o payload UDP para o mesmo formato usado pela GUI."""
        parsed = packet_dict.get("parsed_data", packet_dict)
        if not isinstance(parsed, dict):
            return {}

        def to_float(value: object, default: float) -> float:
            try:
                return float(value)
            except (TypeError, ValueError):
                return default

        telemetry = {
            "position_percent": to_float(parsed.get("position_percent", parsed.get("position", 0.0)), 0.0),
            "trim_hold": bool(parsed.get("trim_hold", False)),
            "beep_trim": str(parsed.get("beep_trim", "NEUTRAL")),
            "pa_active": bool(parsed.get("pa_active", False)),
            "hydraulic_failure": bool(parsed.get("hydraulic_failure", False)),
            "pilot_force_kg": to_float(parsed.get("pilot_force_kg", parsed.get("pilot_force", 0.0)), 0.0),
            "udp_connected": bool(parsed.get("udp_connected", True)),
            "usb_connected": bool(parsed.get("usb_connected", True)),
            "selected_maneuver": str(parsed.get("selected_maneuver", "Manobra 1")),
            "maneuver_active": bool(parsed.get("maneuver_active", False)),
            "maneuver_state": str(parsed.get("maneuver_state", "IDLE")),
            "timestamp": to_float(parsed.get("timestamp", packet_dict.get("timestamp", time.time())), time.time()),
        }
        required_keys = ("position_percent", "pilot_force_kg", "trim_hold")
        if not all(k in parsed for k in required_keys):
            return {}
        return telemetry

    def _apply_dashboard_telemetry(self, data: dict, source: str) -> None:
        """Atualiza os widgets principais com a telemetria recebida."""
        self.position_bar.setValue(int(max(0.0, min(100.0, data["position_percent"])) * 10))
        self.position_display.setText(f"{data['position_percent']:.1f}%")
        self.force_gauge.set_force_kg(data["pilot_force_kg"])

        trim_hold = data["trim_hold"]
        self._set_state(self.trim_hold_lbl, "ok" if trim_hold else "off")
        self._set_state(self.trim_release_lbl, "ok" if not trim_hold else "off")

        beep_trim = data["beep_trim"]
        self._set_state(self.beep_up_lbl, "ok" if beep_trim == "UP" else "off")
        self._set_state(self.beep_down_lbl, "ok" if beep_trim == "DOWN" else "off")

        pa_active = data["pa_active"]
        self._set_state(self.pa_active_lbl, "ok" if pa_active else "off")
        self._set_state(self.pa_override_lbl, "ok" if not pa_active else "off")

        self.udp_led.set_on(data["udp_connected"])
        self.usb_led.set_on(data["usb_connected"])

        has_failure = data["hydraulic_failure"]
        self.alert_lbl.setVisible(has_failure)
        if has_failure and not self.flash_timer.isActive():
            self.flash_timer.start()
        if not has_failure and self.flash_timer.isActive():
            self.flash_timer.stop()
            self.alert_lbl.setProperty("flash", "false")

        # Atualizar tiles de manobra com base no estado recebido
        active = data.get("maneuver_active", False)
        selected = data.get("selected_maneuver", "")

        for name, btn in self.maneuver_buttons.items():
            state = "active" if active and name == selected else "idle"
            if btn.property("runState") != state:
                btn.setProperty("runState", state)
                self._refresh_tile_style(btn)

            should_be_checked = active and name == selected
            if btn.isChecked() != should_be_checked:
                btn.blockSignals(True)
                btn.setChecked(should_be_checked)
                btn.blockSignals(False)

        # Atualizar pane_tile
        pane_on = data.get("hydraulic_failure", False)
        if self.pane_tile.isChecked() != pane_on:
            self.pane_tile.blockSignals(True)
            self.pane_tile.setChecked(pane_on)
            self.pane_tile.blockSignals(False)
        pane_state = "active" if pane_on else "idle"
        if self.pane_tile.property("runState") != pane_state:
            self.pane_tile.setProperty("runState", pane_state)
            self._refresh_tile_style(self.pane_tile)

        # Atualizar hint com status de manobra
        state_text_map = {
            "RUNNING": "Em execução",
            "COMPLETED": "Concluída",
            "ABORTED": "Abortada",
            "IDLE": "Pronta",
        }
        state_raw = data.get("maneuver_state", "IDLE")
        state_text = state_text_map.get(state_raw, state_raw)
        self.maneuver_hint.setText(f"{selected} | Status: {state_text}")

    def _on_udp_packet_received(self, packet_dict: dict) -> None:
        """Handler para pacotes UDP recebidos do Raspberry Pi."""
        self._udp_packet_num = getattr(self, "_udp_packet_num", 0) + 1
        self._last_udp_packet_time = time.time()

        # Atualizar contador
        self.udp_packet_count.setText(f"Pacotes recebidos: {self._udp_packet_num}")

        # Extrair informações do pacote
        sender = packet_dict.get("sender_address", "?")
        port = packet_dict.get("sender_port", "?")
        fmt = packet_dict.get("parse_format", "?")
        length = packet_dict.get("raw_length", 0)

        # Direcionar comandos para o IP que está enviando telemetria
        if isinstance(sender, str) and sender and sender != "?" and sender != self._command_target_host:
            self._command_target_host = sender
            self.command_sender.set_target(receiver_host=self._command_target_host, receiver_port=self._command_target_port)
            self.logger.info(f"Destino de comandos ajustado para {self._command_target_host}:{self._command_target_port}")
            self.udp_endpoints_info.setText(
                f"Escutando telemetria em 0.0.0.0:12345 | Enviando comandos para {self._command_target_host}:{self._command_target_port}"
            )

        self.udp_data_info.setText(
            f"Sensores: {sender}:{port} | Formato: {fmt} | Tamanho: {length} bytes"
        )

        # Mostrar dados parseados
        parsed = packet_dict.get("parsed_data", {})
        if parsed:
            display_text = str(parsed)
            # Limitar a exibição às últimas 2 linhas
            if len(display_text) > 200:
                display_text = "..." + display_text[-200:]
            self.udp_last_packet.setText(display_text)

        telemetry = self._extract_telemetry(packet_dict)
        if telemetry:
            self._apply_dashboard_telemetry(telemetry, source="udp")

    def _on_udp_error(self, error_msg: str) -> None:
        """Handler para erros UDP."""
        self.logger.error(f"UDP Error: {error_msg}")
        self.udp_data_info.setText(f"ERRO: {error_msg}")
        self.udp_status_led.set_on(False)

    def _on_udp_connection_changed(self, connected: bool) -> None:
        """Handler para mudanças no status de conexão UDP."""
        self.udp_status_led.set_on(connected)
        if connected:
            self.udp_data_info.setText("Servidor UDP ativo - aguardando dados do Raspberry Pi...")
        else:
            self.udp_data_info.setText("Servidor UDP desconectado")

    def _on_command_sent(self, command_data: dict) -> None:
        """Handler quando comando é enviado para Raspberry Pi."""
        cmd_type = command_data.get("command_type", "unknown")
        self.logger.info(f"Comando enviado: {cmd_type} - {command_data}")
        if cmd_type == "maneuver":
            maneuver_name = command_data.get("maneuver_name", "?")
            self.maneuver_hint.setText(f"Comando enviado: {maneuver_name}")

    def _on_command_error(self, error_msg: str) -> None:
        """Handler para erros ao enviar comandos."""
        self.logger.error(f"Command Error: {error_msg}")
        self.maneuver_hint.setText(f"Erro ao enviar: {error_msg}")

    def closeEvent(self, event) -> None:
        self.mock_raspberry.stop()
        self.udp_receiver.stop()
        super().closeEvent(event)


def run_app() -> None:
    app = QApplication([])
    app.setStyleSheet(DASHBOARD_QSS)
    window = SccaDashboard()
    window.show()
    app.exec()
