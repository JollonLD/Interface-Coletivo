# Interface SCCA (Dashboard + Raspberry agent)

Este repositório contém a interface gráfica do Sistema de Comando Coletivo Ativo (SCCA) e o agente de telemetria em C++ para Raspberry Pi.

**Visão geral**
- Dashboard: UI em Python (PySide6) responsável por exibir telemetria e enviar comandos.
- Agente Raspberry: programa C++ que lê botões via GPIO e a célula de carga (HX711) e envia telemetria via UDP CSV.

**Principais mudanças recentes**
- O código C++ foi reorganizado para `raspberry_pi/` com a estrutura `include/` + `src/` (subpastas `net/`, `gpio/`, `sensor/`).
- A leitura do HX711 foi migrada para a biblioteca `hx711-1` (wrapper em `src/sensor/hx711_adapter.*`) e usa `isReady()` antes de `read()` (não bloqueante).

**Build & Execução**

Dashboard (Python)
- Instalar dependências:

```bash
pip install -r requirements.txt
```
- Executar:

```bash
python main.py
```

Agente Raspberry (C++)
- Entre em `raspberry_pi/` e rode:

```bash
cd raspberry_pi
make
```

- Isso compila o agente e gera o executável `udp_csv` no diretório `raspberry_pi`.
- Para instalar no sistema (sudo):

```bash
make install
```

Executar o agente:

```bash
./udp_csv <IP_dashboard> <porta_dashboard> [porta_local_escuta]
```

Exemplo:

```bash
./udp_csv 192.168.1.100 12345 12346
```

**Estrutura do repositório (relevante)**
- `main.py` — entrada da aplicação Python (Dashboard).
- `raspberry_pi/` — código do agente C++ e Makefile.
	- `raspberry_pi/include/telemetry.hpp` — tipos e helpers CSV (namespace `scca`).
	- `raspberry_pi/src/main.cpp` — entrypoint do agente.
	- `raspberry_pi/src/net/udp_comm.*` — socket UDP não-bloqueante.
	- `raspberry_pi/src/gpio/gpio_input_reader.*` — leitura de botões (libgpiod).
	- `raspberry_pi/src/sensor/hx711_adapter.*` — wrapper para `hx711-1`.
	- `raspberry_pi/Makefile` — build e targets úteis (all/clean/install/run).

**Variáveis de ambiente (configuração de hardware)**
- `SCCA_USE_GPIO` — `1` para habilitar GPIO real (caso contrário, modo simulado).
- GPIO pins: `SCCA_GPIO_BEEP_UP`, `SCCA_GPIO_BEEP_DOWN`, `SCCA_GPIO_TRIM_RELEASE`, `SCCA_GPIO_OVERRIDE`.
- HX711: `SCCA_GPIO_HX711_DOUT`, `SCCA_GPIO_HX711_SCK`, `SCCA_GPIO_HX711_GAIN_PULSES`.

Exemplo:

```bash
export SCCA_USE_GPIO=1
export SCCA_GPIO_BEEP_UP=17
export SCCA_GPIO_BEEP_DOWN=27
export SCCA_GPIO_HX711_DOUT=5
export SCCA_GPIO_HX711_SCK=6
./udp_csv 192.168.1.100 12345
```

**Dependências de sistema (Raspberry Pi)**
- `libgpiod-dev` — para acesso a GPIO via libgpiod.
- `libhx711` — a biblioteca `hx711-1` (há um subdiretório `hx711/` no repositório; execute `cd hx711 && make && sudo make install` para instalar quando necessário).

Instalação de exemplo:

```bash
sudo apt update
sudo apt install build-essential libgpiod-dev git unzip wget
# Opcional: instalar lgpio e hx711 via scripts abaixo
```

Instalação automática (lgpio + hx711)

Dentro de `raspberry_pi/` existe um script que compila e instala `lgpio` e `hx711` localmente:

```bash
cd raspberry_pi
bash scripts/install_libs.sh
```

O script executa (equivalente a):

lgpio:

```bash
wget http://abyz.me.uk/lg/lg.zip
unzip lg.zip
cd lg
make
sudo make install
```

hx711:

```bash
git clone https://github.com/endail/hx711
cd hx711
make && sudo make install
```

**Logs / Debug**
- O agente C++ imprime `TX C: ...` quando envia telemetria e `RX P: ...` quando recebe comandos. Use esse log para depuração.

**Testes e simulação**
- Você pode rodar o dashboard em modo simulado (sem Raspberry) e usar scripts de test para enviar pacotes UDP. Se desejar, eu adiciono scripts de simulação atualizados ou um README específico em `raspberry_pi/` com exemplos step-by-step.

---
Se quiser, atualizo também um README interno em `raspberry_pi/` com exemplos de deploy (scp/ssh) e um target `deploy` no `Makefile`.
