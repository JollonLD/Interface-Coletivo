#!/bin/bash
# Configura IP fixo 10.0.0.1 na interface ethernet da Raspberry Pi
# (link direto com o dashboard em 10.0.0.5).
#
# Uso na Raspberry:
#   sudo bash configure-static-ip.sh
#   sudo bash configure-static-ip.sh eth0
#
# Requer NetworkManager (Raspberry Pi OS Bookworm+).

set -euo pipefail

IFACE="${1:-eth0}"
PI_IP="10.0.0.1"
PREFIX=24

if ! command -v nmcli >/dev/null 2>&1; then
    echo "nmcli nao encontrado. Configure manualmente em /etc/dhcpcd.conf ou dhcpcd:"
    echo "  interface ${IFACE}"
    echo "  static ip_address=${PI_IP}/${PREFIX}"
    exit 1
fi

echo "Configurando ${IFACE} com IP estatico ${PI_IP}/${PREFIX}..."
nmcli con down "${IFACE}" 2>/dev/null || true
nmcli con delete "${IFACE}-static" 2>/dev/null || true
nmcli con add type ethernet ifname "${IFACE}" con-name "${IFACE}-static" \
    ipv4.method manual ipv4.addresses "${PI_IP}/${PREFIX}" \
    ipv6.method ignore autoconnect yes
nmcli con up "${IFACE}-static"

echo "Pronto. Teste do dashboard (10.0.0.5): ping ${PI_IP}"
ip -4 addr show dev "${IFACE}"
