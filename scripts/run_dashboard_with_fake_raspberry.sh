#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON_BIN="$ROOT_DIR/.venv/bin/python"
SIM_LOG="$ROOT_DIR/.logs/fake_raspberry.log"

mkdir -p "$ROOT_DIR/.logs"

if [[ ! -x "$PYTHON_BIN" ]]; then
  echo "ERRO: Python do venv nao encontrado em $PYTHON_BIN" >&2
  exit 2
fi

: > "$SIM_LOG"

echo "[1/2] Iniciando simulador de telemetria C em background..."
(
  cd "$ROOT_DIR"
  "$PYTHON_BIN" scripts/simulate_raspberry_telemetry.py --host 127.0.0.1 --port 12345 --interval-ms 50
) >> "$SIM_LOG" 2>&1 &
SIM_PID=$!

echo "Simulador PID: $SIM_PID"
echo "Log em tempo real: tail -f $SIM_LOG"

cleanup() {
  if kill -0 "$SIM_PID" 2>/dev/null; then
    kill "$SIM_PID" 2>/dev/null || true
    wait "$SIM_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

echo "[2/2] Abrindo dashboard (feche a janela para encerrar tudo)..."
cd "$ROOT_DIR"
"$PYTHON_BIN" main.py
