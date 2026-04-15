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

## Estrutura

- `main.py`: ponto de entrada
- `scca/dashboard.py`: UI principal e widgets customizados
- `scca/data_worker.py`: thread de dados + mock
- `scca/styles.py`: QSS central do painel
