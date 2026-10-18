# Configuracao do firmware - ponte BLE <-> RS-232 para Agilent/Keysight E3634A
# Placa alvo: ESP32-C3 Super Mini (MicroPython)

# ---------------- RS-232 (via conversor MAX3232 RS232<->TTL) ----------------
UART_ID = 1
UART_TX = 21          # GPIO21 -> T1IN do MAX3232 -> pino 2 (RX) da fonte
UART_RX = 20          # GPIO20 <- R1OUT do MAX3232 <- pino 3 (TX) da fonte
BAUD = 9600           # padrao de fabrica da E3634A (menu I/O no painel)
BITS = 8              # 8 bits + sem paridade (padrao "NONE 8 BITS")
PARITY = None         # None | 0 (par) | 1 (impar) -> use 7 bits se usar paridade
STOP = 2              # a E363x transmite 2 stop bits; use 1 se tiver problemas

# Handshake DTR/DSR opcional (usa o 2o canal do MAX3232).
# Se None: faca o jumper pino 4 <-> pino 6 no DB9 da fonte (ver docs/HARDWARE.md)
DTR_IN_PIN = None     # GPIO que le o DTR da fonte (R2OUT) - fonte pronta p/ receber
DSR_OUT_PIN = None    # GPIO que aciona o DSR da fonte (T2IN) - ESP pronto p/ receber

TIMEOUT_MS = 2500     # timeout de resposta a uma query (MEAS? e lento)
COMPOUND_QUERY = True # usa "MEAS:VOLT?;:MEAS:CURR?;..." numa so transacao
LOCK_FRONT_PANEL = False  # True -> SYST:RWL (trava o painel em modo remoto)

# ---------------- BLE ----------------
BLE_NAME = "E3634A-BLE"

# ---------------- Medicao ----------------
POLL_MS = 500         # intervalo padrao de leitura V/I (alteravel pelo app)
POLL_MIN_MS = 150
RECONNECT_MS = 3000   # tentativa de religar com a fonte quando sem resposta

# ---------------- LED onboard ----------------
LED_PIN = 8           # LED azul da Super Mini (GPIO8, ativo em nivel baixo)
LED_ACTIVE_LOW = True

DEBUG = False
