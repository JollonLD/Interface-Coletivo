#!/usr/bin/env python3
from __future__ import annotations

import argparse
import select
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


@dataclass
class TelemetryC:
    beep_trim_up: int
    beep_trim_down: int
    trim_release: int
    override_state: int
    load_cell: int


@dataclass
class ControlP:
    autopilot_active: int
    hydraulic_failure: int
    transducer_position: int


def parse_c(payload: str) -> TelemetryC | None:
    parts = [p.strip() for p in payload.strip().split(",")]
    if len(parts) != 6 or parts[0] != "C":
        return None
    try:
        beep_up = int(parts[1])
        beep_down = int(parts[2])
        trim_release = int(parts[3])
        override_state = int(parts[4])
        load_cell = int(parts[5])
    except ValueError:
        return None

    if beep_up not in (0, 1):
        return None
    if beep_down not in (0, 1):
        return None
    if trim_release not in (0, 1):
        return None
    if override_state not in (0, 1):
        return None

    return TelemetryC(
        beep_trim_up=beep_up,
        beep_trim_down=beep_down,
        trim_release=trim_release,
        override_state=override_state,
        load_cell=load_cell,
    )


def build_p(p: ControlP) -> str:
    return f"P,{p.autopilot_active},{p.hydraulic_failure},{p.transducer_position}"


def drain_simulator_output(proc: subprocess.Popen[str]) -> tuple[int, int]:
    rx_p_count = 0
    tx_c_count = 0
    if proc.stdout is None:
        return (0, 0)

    while True:
        ready, _, _ = select.select([proc.stdout], [], [], 0)
        if not ready:
            break

        line = proc.stdout.readline()
        if not line:
            break

        if "RX P:" in line:
            rx_p_count += 1
        if "TX C:" in line:
            tx_c_count += 1

    return (rx_p_count, tx_c_count)


def run_test(sim_bin: Path, duration_s: float, interval_ms: int, dashboard_port: int, raspberry_port: int) -> int:
    if not sim_bin.exists():
        print(f"ERRO: binario nao encontrado: {sim_bin}", file=sys.stderr)
        return 2

    proc = subprocess.Popen(
        [str(sim_bin), "127.0.0.1", str(dashboard_port), str(raspberry_port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", dashboard_port))
    sock.settimeout(0.01)

    start = time.monotonic()
    next_send = start
    send_count = 0
    recv_count = 0
    invalid_count = 0
    rx_p_logged = 0
    tx_c_logged = 0
    last_valid: TelemetryC | None = None

    try:
        while (time.monotonic() - start) < duration_s:
            now = time.monotonic()
            if now >= next_send:
                elapsed = now - start
                autopilot = 1 if (elapsed % 4.0) < 2.0 else 0
                hydraulic = 1 if (elapsed % 10.0) > 8.0 else 0
                transducer = int((elapsed * 200.0) % 25000)
                payload = build_p(
                    ControlP(
                        autopilot_active=autopilot,
                        hydraulic_failure=hydraulic,
                        transducer_position=transducer,
                    )
                )
                sock.sendto(payload.encode("utf-8"), ("127.0.0.1", raspberry_port))
                send_count += 1
                next_send += interval_ms / 1000.0

            try:
                data, _ = sock.recvfrom(512)
                text = data.decode("utf-8", errors="ignore").strip()
                parsed = parse_c(text)
                if parsed is None:
                    invalid_count += 1
                else:
                    recv_count += 1
                    last_valid = parsed
            except TimeoutError:
                pass

            add_rx_p, add_tx_c = drain_simulator_output(proc)
            rx_p_logged += add_rx_p
            tx_c_logged += add_tx_c

            if proc.poll() is not None:
                print("ERRO: simulador C++ encerrou antes do fim do teste.")
                return 3

        time.sleep(0.05)
        add_rx_p, add_tx_c = drain_simulator_output(proc)
        rx_p_logged += add_rx_p
        tx_c_logged += add_tx_c

    finally:
        sock.close()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=1.5)
            except subprocess.TimeoutExpired:
                proc.kill()

    print("\n=== Resultado do teste UDP fim a fim ===")
    print(f"Envios P (dashboard -> raspberry): {send_count}")
    print(f"Recebidos C validos (raspberry -> dashboard): {recv_count}")
    print(f"Pacotes C invalidos: {invalid_count}")
    print(f"Logs do simulador RX P: {rx_p_logged}")
    print(f"Logs do simulador TX C: {tx_c_logged}")
    if last_valid is not None:
        print(
            "Ultimo C valido: "
            f"C,{last_valid.beep_trim_up},{last_valid.beep_trim_down},"
            f"{last_valid.trim_release},{last_valid.override_state},{last_valid.load_cell}"
        )

    ok = True
    if send_count < 20:
        print("FALHA: poucos pacotes P enviados (esperado >= 20).")
        ok = False
    if recv_count < 20:
        print("FALHA: poucos pacotes C validos recebidos (esperado >= 20).")
        ok = False
    if rx_p_logged == 0:
        print("FALHA: simulador nao registrou recebimento de P.")
        ok = False
    if tx_c_logged == 0:
        print("FALHA: simulador nao registrou envio de C.")
        ok = False

    if ok:
        print("SUCESSO: protocolo C/P validado localmente sem Raspberry.")
        return 0
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Teste UDP fim a fim sem hardware")
    parser.add_argument("--sim-bin", default="/tmp/udp_csv_test", help="Caminho do simulador C++")
    parser.add_argument("--duration", type=float, default=6.0, help="Duracao do teste em segundos")
    parser.add_argument("--interval-ms", type=int, default=50, help="Intervalo de envio de P (50-150)")
    parser.add_argument("--dashboard-port", type=int, default=12345, help="Porta de telemetria no dashboard")
    parser.add_argument("--raspberry-port", type=int, default=12346, help="Porta de controle no raspberry")
    args = parser.parse_args()

    interval_ms = max(50, min(150, int(args.interval_ms)))
    return run_test(
        sim_bin=Path(args.sim_bin),
        duration_s=max(1.0, args.duration),
        interval_ms=interval_ms,
        dashboard_port=args.dashboard_port,
        raspberry_port=args.raspberry_port,
    )


if __name__ == "__main__":
    raise SystemExit(main())
