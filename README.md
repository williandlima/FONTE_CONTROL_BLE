# FONTE_CONTROL_BLE

Controle total e leitura de tensão/corrente da fonte **Agilent/Keysight E3634A**
por **Bluetooth LE**, usando um **ESP32-C3 Super Mini** (firmware em **C / ESP-IDF**) como
ponte **BLE ↔ RS-232 (SCPI)** e um **app Android**.

```
 ┌──────────────┐   BLE (NUS/JSON)   ┌──────────────────┐  UART TTL  ┌─────────┐  RS-232  ┌─────────┐
 │  App Android │ ◄────────────────► │ ESP32-C3 SuperMini│ ◄────────► │ MAX3232 │ ◄──────► │ E3634A  │
 │  (Compose)   │                    │  (C / ESP-IDF)    │  9600 8N2            └─────────┘   SCPI   └─────────┘
 └──────────────┘                    └──────────────────┘
```

## Funcionalidades

- Leitura contínua de **V, A e W** (200 ms – 2 s), indicação **CV/CC**, estado da saída
- Liga/desliga saída, ajuste de **tensão e corrente** (campo numérico + passos finos)
- Faixas **P25V (25 V/7 A)** e **P50V (50 V/4 A)**
- **OVP/OCP**: nível, habilitar, aviso de disparo e limpeza
- **Memórias 1–5** (`*SAV`/`*RCL`)
- Display: liga/desliga, mensagem de texto, beep
- **Trigger** (BUS/IMEDIATO, atraso, níveis, `INIT`, `*TRG`)
- Local/Remoto, travar painel (`SYST:RWL`), `*RST`, fila de erros
- **Gráfico de tendência** V/I, mín/máx, **Ah e Wh acumulados**, exportação **CSV**
- **Console SCPI** livre para qualquer comando do manual
- Reconexão automática BLE e RS-232; validação de limites no ESP e na fonte

## Estrutura

| Pasta | Conteúdo |
|---|---|
| [`firmware/`](firmware) | Firmware em C (ESP-IDF + PlatformIO): `core/` portátil (driver SCPI, protocolo, JSON) e `main/` (BLE, UART, LED, console) |
| [`tests/`](tests) | Simulador da E3634A em C (com o handshake DTR/DSR), testes do núcleo e teste de ponta a ponta |
| [`host/`](host) | `pc_bridge`: o mesmo núcleo C rodando no PC por uma COM (teste com a fonte real sem ESP) |
| [`android/`](android) | App Android (Kotlin + Jetpack Compose, minSdk 26) |
| [`docs/FIRMWARE_C.md`](docs/FIRMWARE_C.md) | Compilar e gravar com o VS Code |
| [`docs/SCPI_E3634A.md`](docs/SCPI_E3634A.md) | RS-232 e SCPI da E3634A, com as fontes do manual |
| [`docs/HARDWARE.md`](docs/HARDWARE.md) | Ligações, DSR, diagnóstico |
| [`docs/PROTOCOLO.md`](docs/PROTOCOLO.md) | Protocolo JSON BLE completo |
| [`legacy/micropython/`](legacy/micropython) | Firmware MicroPython anterior (1.1.5), só para referência |

## 1. Hardware

Veja **[docs/HARDWARE.md](docs/HARDWARE.md)**. Resumo:

- GPIO21 (TX) → MAX3232 T1IN → pino **2** da fonte
- GPIO20 (RX) ← MAX3232 R1OUT ← pino **3** da fonte
- GND ↔ pino **5**; MAX3232 alimentado em **3,3 V**
- **Pino 6 (DSR) em +5 V por 1 kΩ** e **pino 4 (DTR) solto**: a fonte só responde com o DSR verdadeiro. **Não** use jumper 4↔6, ela trava ao responder (ver [docs/HARDWARE.md](docs/HARDWARE.md) e [docs/esquema_cabo_rs232.png](docs/esquema_cabo_rs232.png))
- Fonte em `RS-232`, `9600 BAUD`, `NONE 8 BITS` (menu I/O Config)

## 2. Firmware (ESP32-C3 Super Mini)

Passo a passo completo em **[docs/FIRMWARE_C.md](docs/FIRMWARE_C.md)**. Resumo:

1. VS Code + extensão **PlatformIO IDE**.
2. **Abrir Pasta** → `firmware/`.
3. Barra azul de baixo: **✓ Build**, **→ Upload** (se não conectar: BOOT + RST), **🔌 Monitor**.
4. No Monitor aparecem as linhas SCPI (`>` enviada, `<` recebida) e dá para digitar `help`,
   `diag`, `loop`, `*IDN?` ou um comando do app como `{"cmd":"meas"}`.

Sem instalar nada: baixe o artefato **firmware-esp32c3-supermini** da aba *Actions* do GitHub e
grave `e3634a-ble-completo.bin` em **0x0** pelo https://espressif.github.io/esptool-js/.

O dispositivo anuncia como **`E3634A-BLE`**.

## 3. App Android

Abra a pasta `android/` no **Android Studio** (Ladybug ou mais novo) e rode
em um celular com Android 8+. Ou pela linha de comando:

```bash
cd android
./gradlew assembleDebug        # APK em app/build/outputs/apk/debug/
```

No app: conceda a permissão de Bluetooth → toque no `E3634A-BLE` → pronto.
Abas: **Painel**, **Gráfico**, **Proteção**, **Mais** (display,
trigger, sistema) e **SCPI** (console).

## 4. Testes sem hardware

```bash
make -C tests          # núcleo C: app -> bridge -> driver -> E3634A simulada (relógio virtual)
make -C tests e2e      # ponta a ponta: processos + porta serial virtual (Linux)
host/bin/pc_bridge.exe COM5   # Windows: mesmo núcleo C direto na fonte via USB-RS232
```

O simulador reproduz o handshake real: com **jumper 4↔6** ou **pino 6 solto** a fonte aceita
comandos e nunca responde, exatamente o sintoma da bancada.

## Diagnóstico rápido

| Sintoma | Causa provável |
|---|---|
| App acha o ESP, LED só com flashes curtos, "fonte não responde" | DSR (pino 6) não está em +5 V, jumper 4–6 presente, TX/RX invertidos, baud/paridade diferentes. Rode `diag` no Monitor |
| Display da fonte mostra `RMT` mas sem leitura | Mesmo caso acima: comandos chegam, respostas não (DSR) |
| Erros `-222 Data out of range` | Valor acima do limite da faixa atual |
| `resposta composta invalida` | Use `PSU_COMPOUND_QUERY 0` (padrão) |
| Painel da fonte travado | Modo RWL: use **Modo LOCAL** no app ou desligue "Travar painel" |
