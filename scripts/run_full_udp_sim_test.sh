#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPP_SRC="$ROOT_DIR/raspberry_pi/udp_sender_example.cpp"
SIM_BIN="/tmp/udp_csv_test"
PYTHON_BIN="$ROOT_DIR/.venv/bin/python"

if [[ ! -x "$PYTHON_BIN" ]]; then
  echo "ERRO: Python do venv nao encontrado em $PYTHON_BIN" >&2
  exit 2
fi

echo "[1/2] Compilando simulador C++..."
g++ -std=c++17 -O2 -o "$SIM_BIN" "$CPP_SRC"

echo "[2/2] Executando teste fim a fim UDP (simulado)..."
"$PYTHON_BIN" "$ROOT_DIR/scripts/full_udp_sim_test.py" "$@"
