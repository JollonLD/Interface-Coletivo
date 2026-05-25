#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import socket
import time


def build_packet(t: float) -> str:
    # Sinais variando para facilitar visualizacao no dashboard.
    load_cell = int(500 + 450 * math.sin(1.3 * t))
    load_cell = max(0, load_cell)

    beep_trim_up = 1 if math.sin(1.7 * t) > 0.45 else 0
    beep_trim_down = 1 if math.sin(1.7 * t) < -0.45 else 0

    # Quando os beeps estao ativos, trim fica em release.
    trim_release = 1 if (beep_trim_up or beep_trim_down) else 0

    # Override alterna em janelas para testar os estados PA/OVERRIDE.
    override = 1 if int(t // 4.0) % 2 else 0

    return f"C,{beep_trim_up},{beep_trim_down},{trim_release},{override},{load_cell}"


def main() -> int:
    parser = argparse.ArgumentParser(description="Simulador de telemetria C (Raspberry -> Dashboard)")
    parser.add_argument("--host", default="127.0.0.1", help="Host do dashboard")
    parser.add_argument("--port", type=int, default=12345, help="Porta UDP do dashboard")
    parser.add_argument("--interval-ms", type=int, default=50, help="Intervalo entre pacotes")
    parser.add_argument("--duration", type=float, default=0.0, help="Duracao em segundos (0 = infinito)")
    parser.add_argument("--quiet", action="store_true", help="Nao imprime cada pacote")
    args = parser.parse_args()

    interval_s = max(0.01, args.interval_ms / 1000.0)
    deadline = time.monotonic() + args.duration if args.duration > 0 else None

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(
        f"Enviando telemetria C para {args.host}:{args.port} "
        f"a cada {args.interval_ms} ms"
    )

    start = time.monotonic()
    count = 0
    try:
        while True:
            now = time.monotonic()
            t = now - start
            packet = build_packet(t)
            sock.sendto(packet.encode("utf-8"), (args.host, args.port))
            count += 1

            if not args.quiet:
                print(packet)

            if deadline is not None and now >= deadline:
                break

            time.sleep(interval_s)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()

    print(f"Total de pacotes enviados: {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
