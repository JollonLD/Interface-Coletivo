# Dashboard SCCA (PySide6)

Interface grafica profissional para o **Sistema de Comando Coletivo Ativo (SCCA)**, com tema industrial/aeronautico escuro, telemetria em tempo real e simulacao de dados via thread.

## Requisitos implementados

- Tema dark com destaques neon:
  - Laranja para dados
  - Verde para status OK
  - Vermelho para alertas criticos
- Monitoramento de posicao com gauge vertical (0 a 100%) e mostrador numerico grande.
- Estados do sistema:
  - Trim (HOLD / RELEASE)
  - Beep Trim (UP / DOWN)
  - Piloto Automatico (PA ACTIVE / OVERRIDE)
  - Alerta critico "FALHA HIDRAULICA" com efeito visual de destaque.
- Telemetria em tempo real:
  - Mostrador circular da Forca do Piloto em KG e N
  - Indicadores de conectividade (LEDs virtuais): UDP (Raspberry Pi) e USB (Arduino)
- Painel de testes com botoes:
  - Acoplar PA
  - Gerar Movimento Aleatorio
  - Ativar Pane Hidraulica

## Arquitetura para integracao futura

A aquisicao de dados esta isolada em `scca/data_worker.py`:

- `BaseDataReceiver`: contrato de aquisicao
- `MockDataReceiver`: mock atual (simulacao)
- `DataWorkerThread`: thread de atualizacao a cada 100 ms emitindo `Signal`

Para integrar com requisitos reais (RI-04 e RI-05), substitua o `MockDataReceiver` por um receiver concreto que implemente `receive_data()` via socket UDP e serial USB.

## Executar

1. Instale dependencias:

```bash
pip install -r requirements.txt
```

2. Rode o dashboard:

```bash
python main.py
```

## Teste fim a fim sem Raspberry

Para validar localmente o protocolo UDP CSV completo (Dashboard <-> Raspberry simulado), execute:

```bash
./scripts/run_full_udp_sim_test.sh
```

Esse comando:
- compila `raspberry_pi/udp_sender_example.cpp` em `/tmp/udp_csv_test`
- sobe um teste automatizado que simula a interface enviando pacotes `P,...`
- valida recebimento de pacotes `C,...` e imprime resumo (sucesso/falha)

Opcoes uteis:

```bash
./scripts/run_full_udp_sim_test.sh --duration 10 --interval-ms 100
```

## Modo ao vivo (interface + simulador local)

Para abrir a interface e manter o simulador do Raspberry rodando em paralelo:

```bash
./scripts/run_live_udp_dashboard.sh
```

Em outro terminal, acompanhe os pacotes em tempo real:

```bash
tail -f .logs/udp_simulator.log
```

Voce deve ver linhas como:
- `TX C: C,...` (telemetria enviada para o dashboard)
- `RX P: autopilot=...` (controle recebido do dashboard)

Observacao:
- Nao e travamento: tanto o dashboard quanto o simulador sao processos continuos.
- O script encerra o simulador automaticamente quando voce fecha a janela do dashboard.

## Simular dados variando do Raspberry (telemetria C)

Se quiser apenas simular o envio de telemetria do Raspberry para confirmar animacao do dashboard:

```bash
./scripts/run_dashboard_with_fake_raspberry.sh
```

Esse modo usa `scripts/simulate_raspberry_telemetry.py` para enviar pacotes `C,...` com valores variando em tempo real.

Para ver os pacotes enviados:

```bash
tail -f .logs/fake_raspberry.log
```

## Estrutura

- `main.py`: ponto de entrada
- `scca/dashboard.py`: UI principal e widgets customizados
- `scca/data_worker.py`: thread de dados + mock
- `scca/styles.py`: QSS central do painel
