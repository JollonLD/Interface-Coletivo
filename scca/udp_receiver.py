"""
Módulo de Recepção UDP Assíncrono para Dashboard de Sistemas Embarcados.

Implementa comunicação UDP não-bloqueante via QUdpSocket com tratamento
robusto de erros e sinais Qt para integração com threads GUI.
"""

from __future__ import annotations

import json
import logging
import math
import struct
from dataclasses import dataclass
from typing import Optional

from PySide6.QtCore import QObject, QTimer, Signal
from PySide6.QtNetwork import QHostAddress, QUdpSocket, QAbstractSocket


logger = logging.getLogger(__name__)


@dataclass
class UDPPacket:
    """Estrutura base para pacotes UDP recebidos."""
    timestamp: float
    raw_data: bytes
    source_address: str
    source_port: int


class UDPReceiver(QObject):
    """
    Recebedor UDP assíncrono utilizando QUdpSocket nativo do Qt.

    Não bloqueia a thread principal e emite sinais Qt para notificar
    a GUI sobre novos dados recebidos.

    Sinais:
        - packet_received: Emite (dict) com dados do pacote
        - error_occurred: Emite (str) com descrição do erro
        - connection_status_changed: Emite (bool) True=conectado, False=desconectado
    """

    packet_received = Signal(dict)
    error_occurred = Signal(str)
    connection_status_changed = Signal(bool)

    def __init__(self, host: str = "0.0.0.0", port: int = 12345) -> None:
        """
        Inicializa o receptor UDP.

        Args:
            host: Endereço IP para binding (padrão: 0.0.0.0 para aceitar qualquer interface)
            port: Porta UDP para escuta (padrão: 12345)
        """
        super().__init__()
        self.host = host
        self.port = port
        self.socket: Optional[QUdpSocket] = None
        self._is_connected = False
        self._packet_count = 0
        self._error_count = 0

        # Timer para diagnóstico periódico
        self._diagnostics_timer = QTimer(self)
        self._diagnostics_timer.timeout.connect(self._log_diagnostics)

    def start(self) -> bool:
        """
        Inicia o servidor UDP.

        Retorna:
            bool: True se iniciado com sucesso, False caso contrário.
        """
        try:
            self.socket = QUdpSocket(self)

            # Conectar sinais do socket
            self.socket.readyRead.connect(self._on_ready_read)

            # Bind ao endereço e porta
            if not self.socket.bind(QHostAddress(self.host), self.port):
                error_msg = f"Falha ao fazer bind em {self.host}:{self.port}. Erro: {self.socket.errorString()}"
                logger.error(error_msg)
                self.error_occurred.emit(error_msg)
                return False

            self._is_connected = True
            success_msg = f"Servidor UDP iniciado em {self.host}:{self.port}"
            logger.info(success_msg)
            self.connection_status_changed.emit(True)

            # Iniciar timer de diagnóstico
            self._diagnostics_timer.start(10000)  # A cada 10 segundos

            return True

        except Exception as e:
            error_msg = f"Exceção ao iniciar UDP: {str(e)}"
            logger.error(error_msg, exc_info=True)
            self.error_occurred.emit(error_msg)
            return False

    def stop(self) -> None:
        """Para o servidor UDP e libera recursos."""
        if self._diagnostics_timer.isActive():
            self._diagnostics_timer.stop()

        if self.socket:
            self.socket.close()
            self._is_connected = False
            self.connection_status_changed.emit(False)
            logger.info("Servidor UDP parado")

    def is_connected(self) -> bool:
        """Retorna o estado de conexão do socket."""
        return self._is_connected and self.socket is not None and self.socket.state() == QAbstractSocket.SocketState.BoundState

    def _on_ready_read(self) -> None:
        """Callback disparado quando há dados disponíveis no socket."""
        if not self.socket:
            return

        try:
            while self.socket.hasPendingDatagrams():
                datagram = self.socket.receiveDatagram()
                self._process_datagram(datagram)

        except Exception as e:
            self._error_count += 1
            error_msg = f"Erro ao processar datagrama: {str(e)}"
            logger.error(error_msg, exc_info=True)
            self.error_occurred.emit(error_msg)

    def _process_datagram(self, datagram) -> None:
        """
        Processa um datagrama recebido.

        Tenta parse automático (JSON, estrutura binária) e emite sinal.
        """
        try:
            import time

            # Converter QByteArray para bytes Python
            qbyte_array = datagram.data()
            data = bytes(qbyte_array)
            sender_address = str(datagram.senderAddress().toString())
            sender_port = datagram.senderPort()

            # Tentar parse como JSON
            packet_dict = self._parse_packet(data, sender_address, sender_port)

            # Emitir sinal com dados parseados
            self.packet_received.emit(packet_dict)
            self._packet_count += 1

            logger.debug(
                f"Pacote #{self._packet_count} recebido de {sender_address}:{sender_port} "
                f"({len(data)} bytes)"
            )

        except Exception as e:
            self._error_count += 1
            error_msg = f"Erro ao processar datagrama: {str(e)}"
            logger.error(error_msg, exc_info=True)
            self.error_occurred.emit(error_msg)

    @staticmethod
    def _parse_packet(data: bytes, sender_address: str, sender_port: int) -> dict:
        """
        Parse inteligente do pacote recebido.

        Tenta JSON first, depois fallback para interpretação genérica.

        Args:
            data: Bytes brutos do datagrama
            sender_address: IP do sender
            sender_port: Porta do sender

        Retorna:
            dict com chaves: raw_hex, parsed_data, sender_address, sender_port, timestamp
        """
        import time

        result = {
            "timestamp": time.time(),
            "sender_address": sender_address,
            "sender_port": sender_port,
            "raw_hex": data.hex(),
            "raw_length": len(data),
            "parsed_data": None,
            "parse_format": "hex_raw",
        }

        # Tentar JSON
        try:
            decoded_str = data.decode("utf-8")
            parsed = json.loads(decoded_str)
            result["parsed_data"] = parsed
            result["parse_format"] = "json"
            return result
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass

        # Tentar interpretação como string ASCII
        try:
            decoded_str = data.decode("utf-8", errors="ignore").strip()
            if decoded_str:
                result["parsed_data"] = {"text": decoded_str}
                result["parse_format"] = "ascii_text"
                return result
        except Exception:
            pass

        # Tentar interpretar como estrutura binária simples (exemplo: float + int)
        if len(data) >= 8:
            try:
                # Exemplo: primeira parte float (4 bytes), segunda parte int (4 bytes)
                float_val = struct.unpack("<f", data[0:4])[0]
                int_val = struct.unpack("<I", data[4:8])[0]
                result["parsed_data"] = {"float_value": float_val, "int_value": int_val}
                result["parse_format"] = "binary_float_int"
                return result
            except struct.error:
                pass

        return result

    def _log_diagnostics(self) -> None:
        """Log periódico de diagnóstico."""
        status = "CONECTADO" if self.is_connected() else "DESCONECTADO"
        logger.info(
            f"UDP Diagnostics - Status: {status} | "
            f"Pacotes: {self._packet_count} | "
            f"Erros: {self._error_count}"
        )


class MockUDPSender(QObject):
    """
    Simulador UDP para testes (emula Raspberry Pi enviando dados de comando coletivo).

    Útil para desenvolvimento e testes sem hardware real.
    """

    def __init__(self, receiver_host: str = "127.0.0.1", receiver_port: int = 12345) -> None:
        super().__init__()
        self.receiver_host = receiver_host
        self.receiver_port = receiver_port
        self.socket = QUdpSocket(self)
        self._send_timer = QTimer(self)
        self._send_timer.timeout.connect(self._send_test_packet)
        self._packet_num = 0

    def start(self, interval_ms: int = 1000) -> None:
        """Inicia envio periódico de pacotes de teste."""
        self._send_timer.start(interval_ms)
        logger.info(f"MockUDPSender iniciado (intervalo: {interval_ms}ms)")

    def stop(self) -> None:
        """Para o envio de pacotes."""
        self._send_timer.stop()
        logger.info("MockUDPSender parado")

    def _send_test_packet(self) -> None:
        """Envia um pacote de teste com dados de COMANDO COLETIVO."""
        import time
        import math

        self._packet_num += 1

        # Simular telemetria de comando coletivo
        position = 30.0 + 20.0 * math.sin(self._packet_num * 3.14159 / 10.0)
        trim_hold = (self._packet_num % 3) != 0
        beep_trim = "NEUTRAL" if (self._packet_num % 2) == 0 else "UP"
        pa_active = True
        hydraulic_failure = False
        pilot_force = 2.0 + 1.5 * math.sin(self._packet_num * 3.14159 / 5.0)

        # Enviar como JSON (formato recomendado)
        test_data = {
            "position_percent": position,
            "trim_hold": trim_hold,
            "beep_trim": beep_trim,
            "pa_active": pa_active,
            "hydraulic_failure": hydraulic_failure,
            "pilot_force_kg": pilot_force,
            "udp_connected": True,
            "usb_connected": True,
            "selected_maneuver": "Circuito Classico",
            "maneuver_active": False,
            "maneuver_state": "IDLE",
            "timestamp": time.time(),
        }

        data = json.dumps(test_data).encode("utf-8")
        self.socket.writeDatagram(
            data,
            QHostAddress(self.receiver_host),
            self.receiver_port,
        )

        logger.debug(f"Pacote de teste #{self._packet_num} enviado: Position={position:.1f}%, Force={pilot_force:.2f}kg")


class CommandSender(QObject):
    """
    Envia comandos de manobra para o Raspberry Pi via UDP.
    
    Permite que o dashboard envie comandos de autopiloto (manobras)
    para o Raspberry Pi executar.
    """
    
    command_sent = Signal(dict)  # Sinal quando comando é enviado
    error_occurred = Signal(str)  # Sinal de erro ao enviar
    
    def __init__(self, receiver_host: str = "127.0.0.1", receiver_port: int = 12346) -> None:
        """
        Inicializa o enviador de comandos.
        
        Args:
            receiver_host: IP do Raspberry Pi (ou 127.0.0.1 para localhost)
            receiver_port: Porta UDP no Raspberry Pi (padrão: 12346 para comandos)
        """
        super().__init__()
        self.receiver_host = receiver_host
        self.receiver_port = receiver_port
        self.socket = QUdpSocket(self)

    def set_target(self, receiver_host: str, receiver_port: Optional[int] = None) -> None:
        """Atualiza dinamicamente o destino dos comandos (IP/porta do Raspberry Pi)."""
        self.receiver_host = receiver_host
        if receiver_port is not None:
            self.receiver_port = receiver_port
        logger.info(f"Destino de comandos atualizado para {self.receiver_host}:{self.receiver_port}")
    
    def send_maneuver_command(
        self,
        maneuver_name: str,
        parameters: dict | None = None,
        action: str = "start",
    ) -> bool:
        """
        Envia um comando de manobra para o Raspberry Pi.
        
        Args:
            maneuver_name: Nome da manobra (ex: "Circuito Classico", "8 Normais", etc)
            parameters: Dicionário opcional com parâmetros adicionais
            
        Returns:
            bool: True se enviado com sucesso, False caso contrário
        """
        try:
            import time
            
            command_data = {
                "command_type": "maneuver",
                "maneuver_name": maneuver_name,
                "action": action,
                "parameters": parameters or {},
                "timestamp": time.time(),
            }
            
            data = json.dumps(command_data).encode("utf-8")
            sent = self.socket.writeDatagram(
                data,
                QHostAddress(self.receiver_host),
                self.receiver_port,
            )
            
            if sent == len(data):
                logger.info(
                    f"Comando enviado: manobra '{maneuver_name}' ({action}) para "
                    f"{self.receiver_host}:{self.receiver_port}"
                )
                self.command_sent.emit(command_data)
                return True
            else:
                error_msg = f"Falha ao enviar comando: enviados {sent} de {len(data)} bytes"
                logger.error(error_msg)
                self.error_occurred.emit(error_msg)
                return False
                
        except Exception as e:
            error_msg = f"Exceção ao enviar comando: {str(e)}"
            logger.error(error_msg)
            self.error_occurred.emit(error_msg)
            return False

    def send_maneuver_stop(self, maneuver_name: str) -> bool:
        """Envia comando para parar/cancelar uma manobra."""
        return self.send_maneuver_command(maneuver_name=maneuver_name, parameters={}, action="stop")

    def send_system_command(self, command: str, value: object) -> bool:
        """Envia comando de sistema (ex.: set_hydraulic_failure) para o Raspberry Pi."""
        try:
            import time

            command_data = {
                "command_type": "system",
                "command": command,
                "value": value,
                "timestamp": time.time(),
            }

            data = json.dumps(command_data).encode("utf-8")
            sent = self.socket.writeDatagram(
                data,
                QHostAddress(self.receiver_host),
                self.receiver_port,
            )

            if sent == len(data):
                logger.info(
                    f"Comando de sistema enviado: {command}={value} para {self.receiver_host}:{self.receiver_port}"
                )
                self.command_sent.emit(command_data)
                return True

            error_msg = f"Falha ao enviar comando de sistema: enviados {sent} de {len(data)} bytes"
            logger.error(error_msg)
            self.error_occurred.emit(error_msg)
            return False

        except Exception as e:
            error_msg = f"Exceção ao enviar comando de sistema: {str(e)}"
            logger.error(error_msg)
            self.error_occurred.emit(error_msg)
            return False

    def send_control_command(self, position_percent: float, trim_hold: bool, beep_trim: str = "NEUTRAL") -> bool:
        """
        Envia um comando de controle direto para o Raspberry Pi.

        Args:
            position_percent: Posição desejada (0-100%)
            trim_hold: Se deve ativar o hold do trim
            beep_trim: Direção do beep ("UP", "DOWN", "NEUTRAL")

        Returns:
            bool: True se enviado com sucesso
        """
        try:
            import time

            command_data = {
                "command_type": "control",
                "position_percent": float(position_percent),
                "trim_hold": bool(trim_hold),
                "beep_trim": str(beep_trim),
                "timestamp": time.time(),
            }

            data = json.dumps(command_data).encode("utf-8")
            sent = self.socket.writeDatagram(
                data,
                QHostAddress(self.receiver_host),
                self.receiver_port,
            )

            if sent == len(data):
                logger.debug(f"Comando de controle enviado: {command_data}")
                self.command_sent.emit(command_data)
                return True

            error_msg = f"Falha ao enviar controle: enviados {sent} de {len(data)} bytes"
            logger.error(error_msg)
            self.error_occurred.emit(error_msg)
            return False

        except Exception as e:
            error_msg = f"Exceção ao enviar controle: {str(e)}"
            logger.error(error_msg)
            self.error_occurred.emit(error_msg)
            return False


class MockRaspberryAutopilot(QObject):
    """
    Simula um Raspberry Pi local:
    - recebe comandos UDP (porta de comando)
    - envia telemetria UDP (porta do dashboard)
    """

    status_changed = Signal(bool)
    error_occurred = Signal(str)

    def __init__(
        self,
        command_host: str = "127.0.0.1",
        command_port: int = 12346,
        telemetry_host: str = "127.0.0.1",
        telemetry_port: int = 12345,
    ) -> None:
        super().__init__()
        self.command_host = command_host
        self.command_port = command_port
        self.telemetry_host = telemetry_host
        self.telemetry_port = telemetry_port

        self.command_socket: Optional[QUdpSocket] = None
        self.telemetry_socket = QUdpSocket(self)
        self.timer = QTimer(self)
        self.timer.timeout.connect(self._tick)
        self._running = False

        self.position_percent = 8.0
        self.trim_hold = True
        self.beep_trim = "NEUTRAL"
        self.pa_active = True
        self.hydraulic_failure = False
        self.pilot_force_kg = 0.0
        self.udp_connected = True
        self.usb_connected = True
        self.selected_maneuver = "Manobra 1"
        self.maneuver_active = False
        self.maneuver_state = "IDLE"
        self._t = 0.0
        self._dt = 0.1

    def start(self, interval_ms: int = 100) -> bool:
        try:
            self.command_socket = QUdpSocket(self)
            if not self.command_socket.bind(QHostAddress(self.command_host), self.command_port):
                msg = (
                    f"MockRaspberry bind falhou em {self.command_host}:{self.command_port} "
                    f"- {self.command_socket.errorString()}"
                )
                logger.error(msg)
                self.error_occurred.emit(msg)
                return False

            self.command_socket.readyRead.connect(self._on_command_ready_read)
            self.timer.start(interval_ms)
            self._running = True
            logger.info(
                f"MockRaspberry ativo: cmd {self.command_host}:{self.command_port} -> "
                f"telem {self.telemetry_host}:{self.telemetry_port}"
            )
            self.status_changed.emit(True)
            return True
        except Exception as e:
            msg = f"Falha ao iniciar MockRaspberry: {e}"
            logger.error(msg)
            self.error_occurred.emit(msg)
            return False

    def stop(self) -> None:
        if self.timer.isActive():
            self.timer.stop()
        if self.command_socket:
            self.command_socket.close()
            self.command_socket = None
        self._running = False
        self.status_changed.emit(False)
        logger.info("MockRaspberry parado")

    def _on_command_ready_read(self) -> None:
        if not self.command_socket:
            return
        while self.command_socket.hasPendingDatagrams():
            dg = self.command_socket.receiveDatagram()
            payload = bytes(dg.data())
            try:
                msg = json.loads(payload.decode("utf-8"))
            except Exception:
                continue

            cmd_type = msg.get("command_type")
            if cmd_type == "maneuver":
                action = str(msg.get("action", "start")).lower()
                maneuver_name = str(msg.get("maneuver_name", self.selected_maneuver))

                if action == "stop":
                    self.maneuver_active = False
                    self.maneuver_state = "IDLE"
                    self.trim_hold = True
                else:
                    self.selected_maneuver = maneuver_name
                    self.maneuver_active = True
                    self.maneuver_state = "RUNNING"
                    self._t = 0.0
                    self.trim_hold = False
            elif cmd_type == "system" and msg.get("command") == "set_hydraulic_failure":
                self.hydraulic_failure = bool(msg.get("value", False))
                if self.hydraulic_failure:
                    self.maneuver_active = False
                    self.maneuver_state = "ABORTED"
                    self.trim_hold = True

    def _profile(self, name: str, t: float) -> float:
        if name == "Manobra 2":
            return 45.0 + 20.0 * math.sin(0.6 * t)
        if name == "Manobra 3":
            return 50.0 + 16.0 * math.sin(0.9 * t + 1.2)
        if name == "Manobra 4":
            phase = int((t // 1.5) % 4)
            levels = [20.0, 40.0, 60.0, 35.0]
            return levels[phase]
        return 40.0 + 18.0 * math.sin(0.5 * t)

    def _tick(self) -> None:
        import time

        if self.hydraulic_failure:
            self.position_percent = 0.0
            self.pilot_force_kg = 0.0
            self.beep_trim = "DOWN"
            self.trim_hold = True
        elif self.maneuver_active:
            prev = self.position_percent
            self.position_percent = max(0.0, min(100.0, self._profile(self.selected_maneuver, self._t)))
            slope = self.position_percent - prev
            if slope > 0.2:
                self.beep_trim = "UP"
            elif slope < -0.2:
                self.beep_trim = "DOWN"
            else:
                self.beep_trim = "NEUTRAL"
            self.pilot_force_kg = max(0.0, min(8.0, abs(slope) * 0.8 + 1.2))
            self._t += self._dt
            if self._t >= 20.0:
                self.maneuver_active = False
                self.maneuver_state = "COMPLETED"
                self.trim_hold = True
                self.beep_trim = "NEUTRAL"
        else:
            self.position_percent = self.position_percent
            self.pilot_force_kg = max(0.0, self.pilot_force_kg * 0.95)
            if self.maneuver_state not in ("COMPLETED", "ABORTED"):
                self.maneuver_state = "IDLE"
            self.beep_trim = "NEUTRAL"

        telemetry = {
            "position_percent": self.position_percent,
            "trim_hold": self.trim_hold,
            "beep_trim": self.beep_trim,
            "pa_active": self.pa_active,
            "hydraulic_failure": self.hydraulic_failure,
            "pilot_force_kg": self.pilot_force_kg,
            "udp_connected": self.udp_connected,
            "usb_connected": self.usb_connected,
            "selected_maneuver": self.selected_maneuver,
            "maneuver_active": self.maneuver_active,
            "maneuver_state": self.maneuver_state,
            "timestamp": time.time(),
        }

        data = json.dumps(telemetry).encode("utf-8")
        self.telemetry_socket.writeDatagram(
            data,
            QHostAddress(self.telemetry_host),
            self.telemetry_port,
        )

