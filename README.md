# FONTE_CONTROL_BLE

Controle total e leitura de tensão/corrente da fonte **Agilent/Keysight E3634A**
por **Bluetooth LE**, usando um **ESP32-C3 Super Mini** (MicroPython) como ponte
**BLE ↔ RS-232 (SCPI)** e um **app Android**.

```
 ┌──────────────┐   BLE (NUS/JSON)   ┌──────────────────┐  UART TTL  ┌─────────┐  RS-232  ┌─────────┐
 │  App Android │ ◄────────────────► │ ESP32-C3 SuperMini│ ◄────────► │ MAX3232 │ ◄──────► │ E3634A  │
 │  (Compose)   │                    │   (MicroPython)   │  9600 8N2  └─────────┘   SCPI   └─────────┘
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
| [`firmware/`](firmware) | MicroPython para o ESP32-C3 (`main.py`, `config.py`, driver `e3634a.py`, protocolo `bridge.py`, BLE `ble_uart.py`) |
| [`android/`](android) | App Android (Kotlin + Jetpack Compose, minSdk 26) |
| [`tests/`](tests) | Simulador da E3634A e testes do firmware (rodam no PC) |
| [`tools/pc_bench.py`](tools/pc_bench.py) | Terminal SCPI no PC via USB-RS232 (valida cabo/config sem ESP32) |
| [`docs/HARDWARE.md`](docs/HARDWARE.md) | Ligações, jumper DTR/DSR, config da fonte |
| [`docs/PROTOCOLO.md`](docs/PROTOCOLO.md) | Protocolo JSON BLE completo |

## 1. Hardware

Veja **[docs/HARDWARE.md](docs/HARDWARE.md)**. Resumo:

- GPIO21 (TX) → MAX3232 T1IN → pino **2** da fonte
- GPIO20 (RX) ← MAX3232 R1OUT ← pino **3** da fonte
- GND ↔ pino **5**; MAX3232 alimentado em **3,3 V**
- **Jumper pino 4 ↔ pino 6** no DB9 da fonte (handshake DTR/DSR — sem isso a fonte não responde)
- Fonte em `RS-232`, `9600 BAUD`, `NONE 8 BITS` (menu I/O Config)

## 2. Firmware (ESP32-C3 Super Mini)

1. Grave o MicroPython para **ESP32-C3** (https://micropython.org/download/ESP32_GENERIC_C3/):
   ```bash
   pip install esptool mpremote
   esptool.py --chip esp32c3 --port /dev/ttyACM0 erase_flash
   esptool.py --chip esp32c3 --port /dev/ttyACM0 write_flash -z 0x0 ESP32_GENERIC_C3-*.bin
   ```
   (Se não entrar em modo boot: segure **BOOT**, aperte **RESET**, solte BOOT.)
2. Copie os arquivos:
   ```bash
   cd firmware
   mpremote connect /dev/ttyACM0 cp config.py e3634a.py bridge.py ble_uart.py selftest.py boot.py main.py : + reset
   ```
3. Teste a fiação antes (loopback e sondagem da fonte): `import selftest` no REPL,
   ver [docs/HARDWARE.md](docs/HARDWARE.md#roteiro-de-teste-nesta-ordem).
4. Para ver o log: `DEBUG = True` no `config.py` e `mpremote connect /dev/ttyACM0 repl`.

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
python3 -m unittest discover -s tests -v       # driver + protocolo contra o simulador
python3 tools/pc_bench.py --sim                # terminal SCPI no simulador
python3 tools/pc_bench.py --port /dev/ttyUSB0  # direto na fonte via USB-RS232
```

## Diagnóstico rápido

| Sintoma | Causa provável |
|---|---|
| App acha o ESP, LED pisca rápido, "fonte não responde" | Falta jumper 4–6, TX/RX invertidos, baud/paridade diferentes |
| Display da fonte mostra `RMT` mas sem leitura | Mesmo caso acima: comandos chegam, respostas não (DSR) |
| Erros `-222 Data out of range` | Valor acima do limite da faixa atual |
| `resposta composta invalida` | Use `COMPOUND_QUERY = False` |
| Painel da fonte travado | Modo RWL: use **Modo LOCAL** no app ou desligue "Travar painel" |
