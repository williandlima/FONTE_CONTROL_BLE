# Ponte BLE <-> RS-232 para Agilent E3634A - ESP32-C3 Super Mini / MicroPython

from machine import UART, Pin
from time import sleep_ms, ticks_ms, ticks_diff, ticks_add

import config as C
from e3634a import E3634A
from bridge import Bridge
from ble_uart import BLEUART


# Janela de seguranca: nos 3 s apos ligar, Ctrl+C no Thonny ou segurar BOOT
# (GPIO9) interrompem o firmware e deixam o REPL livre para enviar arquivos.
print("Janela de seguranca (3 s): Ctrl+C ou segure BOOT para ficar no REPL")
_boot = Pin(9, Pin.IN, Pin.PULL_UP)
for _ in range(30):
    if _boot.value() == 0:
        print("BOOT pressionado: firmware nao iniciado")
        raise SystemExit
    sleep_ms(100)


def log(*a):
    if C.DEBUG:
        print(*a)


led = Pin(C.LED_PIN, Pin.OUT)


def led_set(on):
    led.value((not on) if C.LED_ACTIVE_LOW else on)


# LED = atividade da serial: flash curto (40 ms) a cada comando enviado a fonte; se a fonte
# responder, o flash se estende por 200 ms. So curto = fonte muda; longo = fonte respondeu.
# Em repouso apagado; sem app conectado, um piscar rapido a cada 2 s.
_pulse_until = 0
ble = None


def activity(kind):
    global _pulse_until
    now = ticks_ms()
    end = ticks_add(now, 200 if kind == "rx" else 40)
    if ticks_diff(end, now) > ticks_diff(_pulse_until, now):
        _pulse_until = end
    led_set(True)


def led_update():
    now = ticks_ms()
    if ticks_diff(_pulse_until, now) > 0:
        led_set(True)
    elif ble is not None and not ble.connected:
        led_set(now % 2000 < 50)
    else:
        led_set(False)


uart = UART(C.UART_ID, baudrate=C.BAUD, bits=C.BITS, parity=C.PARITY,
            stop=C.STOP, tx=C.UART_TX, rx=C.UART_RX, rxbuf=512, timeout=0)

if C.DSR_OUT_PIN is not None:
    Pin(C.DSR_OUT_PIN, Pin.OUT, value=1)   # ESP sempre pronto p/ receber
dtr_in = Pin(C.DTR_IN_PIN, Pin.IN) if C.DTR_IN_PIN is not None else None

psu = E3634A(uart, timeout_ms=C.TIMEOUT_MS, compound=C.COMPOUND_QUERY,
             dtr_in=dtr_in, log=log if C.DEBUG else None, xonxoff=C.XONXOFF,
             activity=activity, idle=led_update)
ble = BLEUART(C.BLE_NAME)
bridge = Bridge(psu, ble.send_line, poll_ms=C.POLL_MS, poll_min_ms=C.POLL_MIN_MS,
                reconnect_ms=C.RECONNECT_MS, lock_panel=C.LOCK_FRONT_PANEL,
                labtest=C.LABTEST_SEQUENCE)

print("E3634A-BLE pronto. BLE:", C.BLE_NAME)

while True:
    for connected in ble.events():
        log("BLE cliente", "conectado" if connected else "desconectado")
        bridge.on_client(connected)
    for line in ble.lines():
        log("APP>", line)
        bridge.handle_line(line)
    bridge.tick()

    led_update()
    sleep_ms(5)
