"""
EXEMPLO PRÁTICO - Integração UDP Customizada
==============================================

Este arquivo demonstra como estender o sistema UDP para casos específicos.
Copie e adapte conforme suas necesidades.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Optional

from PySide6.QtCore import Signal, QObject
from PySide6.QtWidgets import (
    QFrame,
    QVBoxLayout,
    QLabel,
    QProgressBar,
    QHBoxLayout,
)

from scca.udp_receiver import UDPReceiver


# ============================================================================
# EXEMPLO 1: Parser Customizado para Formato Específico
# ============================================================================

@dataclass
class CustomSensorData:
    """Estrutura esperada dos dados do Raspberry Pi - SISTEMA DE COMANDO COLETIVO."""
    position_percent: float  # Posição coletiva 0-100%
    trim_hold: bool  # Trim mantendo posição
    beep_trim: str  # "UP", "DOWN", "NEUTRAL"
    pa_active: bool  # Power Assist ativo
    hydraulic_failure: bool  # Falha hidráulica detectada
    pilot_force_kg: float  # Força do piloto em kg
    udp_connected: bool  # UDP conectado
    usb_connected: bool  # USB conectado
    selected_maneuver: str  # Nome da manobra selecionada
    maneuver_active: bool  # Manobra em execução
    maneuver_state: str  # "IDLE", "RUNNING", "COMPLETED", "ABORTED"
    timestamp: float  # Timestamp do pacote


class CustomUDPParser:
    """Parser especializado para dados de comando coletivo do Raspberry Pi."""

    @staticmethod
    def parse_sensor_data(packet_dict: dict) -> Optional[CustomSensorData]:
        """
        Tenta converter dados UDP em CustomSensorData.
        
        Suporta JSON com estrutura de telemetria de comando coletivo.
        """
        parsed = packet_dict.get("parsed_data", {})
        
        # Verificar se é string com formato delimitado PRIMEIRO
        if isinstance(parsed, dict) and "text" in parsed:
            try:
                text = parsed["text"]
                # Exemplo: "POS:42.5|TRIM:1|BEEP:NEUTRAL|PA:1|HYD:0|FORCE:2.3|STATE:IDLE"
                parts = {}
                for segment in text.split("|"):
                    if ":" in segment:
                        key, value = segment.split(":", 1)
                        parts[key.strip()] = value.strip()
                
                return CustomSensorData(
                    position_percent=float(parts.get("POS", "0")),
                    trim_hold=bool(int(parts.get("TRIM", "1"))),
                    beep_trim=parts.get("BEEP", "NEUTRAL"),
                    pa_active=bool(int(parts.get("PA", "0"))),
                    hydraulic_failure=bool(int(parts.get("HYD", "0"))),
                    pilot_force_kg=float(parts.get("FORCE", "0")),
                    udp_connected=True,
                    usb_connected=True,
                    selected_maneuver=parts.get("MAN", ""),
                    maneuver_active=bool(int(parts.get("MACT", "0"))),
                    maneuver_state=parts.get("STATE", "IDLE"),
                    timestamp=float(parts.get("TS", "0")),
                )
            except (ValueError, AttributeError, IndexError):
                return None
        
        # Se é JSON (dict com campos de comando coletivo)
        if isinstance(parsed, dict) and parsed and "position_percent" in parsed:
            try:
                return CustomSensorData(
                    position_percent=parsed.get("position_percent", 0.0),
                    trim_hold=parsed.get("trim_hold", True),
                    beep_trim=parsed.get("beep_trim", "NEUTRAL"),
                    pa_active=parsed.get("pa_active", False),
                    hydraulic_failure=parsed.get("hydraulic_failure", False),
                    pilot_force_kg=parsed.get("pilot_force_kg", 0.0),
                    udp_connected=parsed.get("udp_connected", True),
                    usb_connected=parsed.get("usb_connected", True),
                    selected_maneuver=parsed.get("selected_maneuver", ""),
                    maneuver_active=parsed.get("maneuver_active", False),
                    maneuver_state=parsed.get("maneuver_state", "IDLE"),
                    timestamp=parsed.get("timestamp", 0.0),
                )
            except (KeyError, TypeError):
                return None
        
        return None


# ============================================================================
# EXEMPLO 2: Painel Customizado com Métricas
# ============================================================================

class SensorMetricsPanel(QFrame):
    """Painel especializado para exibir métricas de comando coletivo."""

    sensor_data_updated = Signal(CustomSensorData)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setObjectName("panel")
        self.setStyleSheet(
            "QFrame#panel { background-color: rgba(20, 29, 38, 220); "
            "border: 1px solid #283443; border-radius: 12px; }"
        )

        layout = QVBoxLayout(self)

        # Título
        title = QLabel("Telemetria UDP (Raspberry Pi)")
        title.setStyleSheet("font-size: 13px; font-weight: 600; color: #7f93a8;")
        layout.addWidget(title)

        # Posição Coletiva
        pos_row = QHBoxLayout()
        self.pos_label = QLabel("Posição Coletiva: ---%")
        self.pos_label.setStyleSheet("color: #82d8ff; font-size: 12px;")
        self.pos_bar = QProgressBar()
        self.pos_bar.setRange(0, 100)
        self.pos_bar.setMaximumHeight(20)
        pos_row.addWidget(self.pos_label, 0)
        pos_row.addWidget(self.pos_bar, 1)
        layout.addLayout(pos_row)

        # Força do Piloto
        force_row = QHBoxLayout()
        self.force_label = QLabel("Força Piloto: -- kg")
        self.force_label.setStyleSheet("color: #82d8ff; font-size: 12px;")
        self.force_bar = QProgressBar()
        self.force_bar.setRange(0, 40)  # 0-4.0 kg em escala
        self.force_bar.setMaximumHeight(20)
        force_row.addWidget(self.force_label, 0)
        force_row.addWidget(self.force_bar, 1)
        layout.addLayout(force_row)

        # Trim Hold
        self.trim_label = QLabel("Trim: HOLD")
        self.trim_label.setStyleSheet("color: #16ff9a; font-size: 11px; font-weight: bold;")
        layout.addWidget(self.trim_label)

        # Beep Trim
        self.beep_label = QLabel("Beep Trim: NEUTRAL")
        self.beep_label.setStyleSheet("color: #82d8ff; font-size: 11px;")
        layout.addWidget(self.beep_label)

        # PA Status
        self.pa_label = QLabel("PA: ATIVO")
        self.pa_label.setStyleSheet("color: #16ff9a; font-size: 11px; font-weight: bold;")
        layout.addWidget(self.pa_label)

        # Manobra
        self.maneuver_label = QLabel("Manobra: --")
        self.maneuver_label.setStyleSheet("color: #f58f2d; font-size: 11px;")
        layout.addWidget(self.maneuver_label)

        # Conectividade
        conn_row = QHBoxLayout()
        self.udp_conn_label = QLabel("UDP: ✓")
        self.udp_conn_label.setStyleSheet("color: #16ff9a; font-size: 10px;")
        self.usb_conn_label = QLabel("USB: ✓")
        self.usb_conn_label.setStyleSheet("color: #16ff9a; font-size: 10px;")
        self.hyd_label = QLabel("HYD: ✓")
        self.hyd_label.setStyleSheet("color: #16ff9a; font-size: 10px;")
        conn_row.addWidget(self.udp_conn_label)
        conn_row.addWidget(self.usb_conn_label)
        conn_row.addWidget(self.hyd_label)
        conn_row.addStretch(1)
        layout.addLayout(conn_row)

        layout.addStretch(1)

    def update_from_sensor_data(self, data: CustomSensorData) -> None:
        """Atualiza todos os displays com valores da telemetria."""
        self.pos_label.setText(f"Posição Coletiva: {data.position_percent:.1f}%")
        self.pos_bar.setValue(int(data.position_percent))

        self.force_label.setText(f"Força Piloto: {data.pilot_force_kg:.2f} kg")
        self.force_bar.setValue(int(data.pilot_force_kg * 10))  # Escala 0-40 (0-4kg)

        trim_text = "HOLD" if data.trim_hold else "RELEASE"
        trim_color = "#16ff9a" if data.trim_hold else "#f04a54"
        self.trim_label.setText(f"Trim: {trim_text}")
        self.trim_label.setStyleSheet(f"color: {trim_color}; font-size: 11px; font-weight: bold;")

        beep_color = {"UP": "#f58f2d", "DOWN": "#f04a54", "NEUTRAL": "#82d8ff"}[data.beep_trim]
        self.beep_label.setText(f"Beep Trim: {data.beep_trim}")
        self.beep_label.setStyleSheet(f"color: {beep_color}; font-size: 11px;")

        pa_color = "#16ff9a" if data.pa_active else "#f04a54"
        pa_text = "ATIVO" if data.pa_active else "INATIVO"
        self.pa_label.setText(f"PA: {pa_text}")
        self.pa_label.setStyleSheet(f"color: {pa_color}; font-size: 11px; font-weight: bold;")

        maneuver_text = f"{data.selected_maneuver} ({data.maneuver_state})"
        self.maneuver_label.setText(f"Manobra: {maneuver_text}")

        udp_icon = "✓" if data.udp_connected else "✗"
        udp_color = "#16ff9a" if data.udp_connected else "#f04a54"
        self.udp_conn_label.setText(f"UDP: {udp_icon}")
        self.udp_conn_label.setStyleSheet(f"color: {udp_color}; font-size: 10px;")

        usb_icon = "✓" if data.usb_connected else "✗"
        usb_color = "#16ff9a" if data.usb_connected else "#f04a54"
        self.usb_conn_label.setText(f"USB: {usb_icon}")
        self.usb_conn_label.setStyleSheet(f"color: {usb_color}; font-size: 10px;")

        hyd_color = "#f04a54" if data.hydraulic_failure else "#16ff9a"
        hyd_icon = "⚠️" if data.hydraulic_failure else "✓"
        self.hyd_label.setText(f"HYD: {hyd_icon}")
        self.hyd_label.setStyleSheet(f"color: {hyd_color}; font-size: 10px;")

        self.sensor_data_updated.emit(data)


# ============================================================================
# EXEMPLO 3: Gerenciador de UDP Customizado
# ============================================================================

class CustomUDPManager(QObject):
    """
    Gerenciador especializado que conecta UDPReceiver ao SensorMetricsPanel.
    
    Uso:
        manager = CustomUDPManager()
        manager.setup(udp_receiver, sensor_panel)
        # Agora sensor_panel atualiza automaticamente com dados do Raspberry Pi
    """

    def __init__(self) -> None:
        super().__init__()
        self.udp_receiver: Optional[UDPReceiver] = None
        self.sensor_panel: Optional[SensorMetricsPanel] = None
        self._packet_count = 0
        self._error_count = 0

    def setup(self, udp_receiver: UDPReceiver, sensor_panel: SensorMetricsPanel) -> None:
        """Conectar receiver ao painel."""
        self.udp_receiver = udp_receiver
        self.sensor_panel = sensor_panel

        # Conectar sinais
        udp_receiver.packet_received.connect(self._on_packet_received)
        udp_receiver.error_occurred.connect(self._on_error)

    def _on_packet_received(self, packet_dict: dict) -> None:
        """Processar novo pacote UDP."""
        self._packet_count += 1

        # Tentar parse
        sensor_data = CustomUDPParser.parse_sensor_data(packet_dict)

        if sensor_data:
            # Sucesso! Atualizar painel
            self.sensor_panel.update_from_sensor_data(sensor_data)
        else:
            # Falha no parse
            self._error_count += 1
            print(f"Falha ao parsear dados. Raw: {packet_dict.get('raw_hex', 'N/A')}")

    def _on_error(self, error_msg: str) -> None:
        """Lidar com erros."""
        print(f"Erro UDP: {error_msg}")


# ============================================================================
# EXEMPLO 4: Teste do Parser
# ============================================================================

def test_parser() -> None:
    """Teste rápido do parser customizado."""
    import json

    # Simular pacote JSON com dados de comando coletivo
    test_packet_json = {
        "sender_address": "192.168.1.100",
        "sender_port": 54321,
        "parsed_data": {
            "position_percent": 42.5,
            "trim_hold": True,
            "beep_trim": "NEUTRAL",
            "pa_active": True,
            "hydraulic_failure": False,
            "pilot_force_kg": 2.3,
            "udp_connected": True,
            "usb_connected": True,
            "selected_maneuver": "Circuito Classico",
            "maneuver_active": False,
            "maneuver_state": "IDLE",
            "timestamp": 1234567890.5,
        },
        "parse_format": "json",
    }

    result = CustomUDPParser.parse_sensor_data(test_packet_json)
    print(f"Parser test (JSON): {result}")

    # Simular pacote string com dados de comando coletivo
    test_packet_string = {
        "sender_address": "192.168.1.100",
        "sender_port": 54321,
        "parsed_data": {"text": "POS:42.5|TRIM:1|BEEP:NEUTRAL|PA:1|HYD:0|FORCE:2.3|STATE:IDLE"},
        "parse_format": "ascii_text",
    }

    result = CustomUDPParser.parse_sensor_data(test_packet_string)
    print(f"Parser test (String): {result}")


if __name__ == "__main__":
    test_parser()
