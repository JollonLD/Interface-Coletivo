#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPP_SRC="$ROOT_DIR/raspberry_pi/udp_sender_example.cpp"
SIM_BIN="/tmp/udp_csv_test"
PYTHON_BIN="$ROOT_DIR/.venv/bin/python"
LOG_DIR="$ROOT_DIR/.logs"
SIM_LOG="$LOG_DIR/udp_simulator.log"

mkdir -p "$LOG_DIR"

if [[ ! -x "$PYTHON_BIN" ]]; then
  echo "ERRO: Python do venv nao encontrado em $PYTHON_BIN" >&2
  exit 2
fi

echo "[1/3] Compilando simulador C++..."
g++ -std=c++17 -O2 -o "$SIM_BIN" "$CPP_SRC"

echo "[2/3] Iniciando simulador Raspberry em background..."
: > "$SIM_LOG"
(
  cd "$ROOT_DIR"
  stdbuf -oL -eL "$SIM_BIN" 127.0.0.1 12345 12346
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

echo "[3/3] Abrindo dashboard (feche a janela para encerrar tudo)..."
cd "$ROOT_DIR"
"$PYTHON_BIN" main.py
