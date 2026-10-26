# Firmware em C: compilar e gravar com o VS Code

O firmware é C puro sobre o **ESP-IDF** (framework oficial da Espressif), compilado pela
extensão **PlatformIO** do VS Code. Não tem MicroPython nem Thonny: grava-se uma vez e
pronto.

```
firmware/
├── platformio.ini        projeto PlatformIO (placa, porta, velocidades)
├── sdkconfig.defaults    BLE NimBLE, console no USB, 4 MB de flash
├── core/                 lógica portátil, testada no PC
│   ├── config.h          baud, timeouts, sequência de abertura, versão (FW_VERSION)
│   ├── psu.c / psu.h     driver SCPI da E3634A (RS-232, XON/XOFF, erros, sessão)
│   ├── bridge.c / .h     protocolo JSON do app (docs/PROTOCOLO.md)
│   └── json.c / .h       JSON mínimo, sem malloc
└── main/                 só o que depende da placa
    ├── board.h           pinos: TX=GPIO21, RX=GPIO20, LED=GPIO8
    ├── ble_nus.c / .h    BLE Nordic UART Service (NimBLE)
    └── main.c            UART, LED, console USB, laço principal
```

## 1. Instalar (uma vez)

1. **VS Code**: https://code.visualstudio.com
2. Na aba **Extensões** (Ctrl+Shift+X), instale **PlatformIO IDE**. Espere ele terminar de
   instalar (canto inferior direito) e reinicie o VS Code.
3. **Arquivo → Abrir Pasta…** e escolha **`FONTE_CONTROL_BLE\firmware`** (a pasta que tem o
   `platformio.ini`, não a raiz do repositório).

Na primeira compilação o PlatformIO baixa sozinho o ESP-IDF e o compilador RISC-V
(~1 GB, alguns minutos).

## 2. Compilar, gravar e ver o log

Na **barra azul de baixo** do VS Code:

| Ícone | Ação | Atalho |
|---|---|---|
| ✓ | **Build**: compila | Ctrl+Alt+B |
| → | **Upload**: grava na placa | Ctrl+Alt+U |
| 🔌 | **Monitor**: console USB | Ctrl+Alt+S |

1. Ligue a Super Mini no USB **direto no PC**, com cabo de dados.
2. Clique em **→ Upload**. O PlatformIO acha a porta sozinho.
   - Se der *"Failed to connect"* ou não achar a porta: **segure BOOT, aperte e solte RST,
     solte BOOT** e clique em Upload de novo. Depois de gravar, aperte **RST**.
   - Para fixar a porta, descomente `upload_port = COM10` no `platformio.ini`.
3. Clique em **🔌 Monitor**. Deve aparecer:
   ```
   E3634A-BLE firmware 2.0.0 | UART1 TX=GPIO21 RX=GPIO20 9600 8N2 | BLE "E3634A-BLE"
   > <Ctrl-C>
   > *CLS
   > SYST:ERR?
   < +0,"No error"
   > *IDN?
   < HEWLETT-PACKARD,E3634A,0,2.4-6.1-2.1
   > SYST:REM
   I (1234) main: fonte conectada: HEWLETT-PACKARD,E3634A,...
   ```
   `>` é o que o ESP mandou e `<` é o que a fonte respondeu. Só `>` e
   `timeout rx=vazio` significa que a fonte não está respondendo
   ([HARDWARE.md](HARDWARE.md#diagnóstico-pelo-console-usb)).
4. No Monitor dá para digitar `help`, `diag`, `edges`, `loop`, `*IDN?`, `{"cmd":"meas"}` etc.

> Só um programa abre a COM por vez: feche o Monitor antes de fazer Upload (o PlatformIO
> costuma fechar sozinho) e feche Thonny/Arduino/PuTTY.

## 3. Sem PlatformIO: gravar o .bin pronto

Todo push no GitHub roda o workflow **firmware** (aba *Actions*), que compila com o
PlatformIO e publica o artefato **firmware-esp32c3-supermini** com o
`e3634a-ble-completo.bin`.

1. Baixe e descompacte o artefato.
2. No Chrome/Edge, abra https://espressif.github.io/esptool-js/ → **Connect** → escolha a
   porta da Super Mini (se não aparecer: BOOT + RST).
3. *Flash Address* **0x0**, arquivo `e3634a-ble-completo.bin`, **Program**.
4. Aperte RST.

Pelo terminal: `python -m esptool --chip esp32c3 write_flash 0x0 e3634a-ble-completo.bin`.

## 4. Testar sem hardware (no PC)

```bash
make -C tests        # 150+ verificações: app -> bridge -> driver -> E3634A simulada
make -C tests e2e    # processos reais e porta serial virtual (Linux)
```

O simulador reproduz o handshake da fonte: com o **jumper 4↔6** ou o **pino 6 solto** os
testes mostram a fonte aceitando comandos e nunca respondendo, igual à bancada.

## 5. Testar o firmware com a fonte real pelo PC

`host/bin/pc_bridge.exe` é o **mesmo código C** do firmware, falando por uma COM do PC
(adaptador USB-RS232, o mesmo do Lab Test):

```bat
host\bin\pc_bridge.exe COM5
{"cmd":"meas"}
{"cmd":"apply","v":5,"a":0.1}
{"cmd":"out","on":true}
```

Se funcionar no PC e não no ESP, o código está certo e a diferença é elétrica: pino 6 sem
+5 V, jumper 4↔6, ou o módulo MAX3232.

## 6. Mudar configurações

| O quê | Onde |
|---|---|
| Pinos TX/RX/LED, DTR/DSR opcionais | `firmware/main/board.h` |
| Baud, timeout, sequência Lab Test, trava do painel, intervalo de leitura | `firmware/core/config.h` |
| Nome BLE | `BLE_NAME` em `config.h` e `CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME` em `sdkconfig.defaults` |
| Versão (aparece no app) | `FW_VERSION` em `config.h` |

Depois de mudar o `sdkconfig.defaults`, apague `firmware/sdkconfig.supermini` (ou use
*PlatformIO → Clean*) para ele ser regenerado.
