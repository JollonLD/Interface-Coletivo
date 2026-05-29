#!/usr/bin/env bash
set -euo pipefail

echo "==> Instalando lgpio (build local)"
wget -q http://abyz.me.uk/lg/lg.zip -O /tmp/lg.zip
unzip -q /tmp/lg.zip -d /tmp
pushd /tmp/lg > /dev/null
make -j$(nproc)
sudo make install
popd > /dev/null

echo "==> Instalando hx711 (github endail)"
if [ ! -d /tmp/hx711 ]; then
    git clone https://github.com/endail/hx711 /tmp/hx711
else
    echo "/tmp/hx711 já existe — atualizando"
    pushd /tmp/hx711 > /dev/null
    git pull
    popd > /dev/null
fi
pushd /tmp/hx711 > /dev/null
make -j$(nproc)
sudo make install
popd > /dev/null

echo "Instalação concluída."
