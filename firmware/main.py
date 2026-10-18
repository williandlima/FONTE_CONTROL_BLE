# Ponte BLE <-> RS-232 para Agilent E3634A - ESP32-C3 Super Mini / MicroPython

from machine import UART, Pin
from time import sleep_ms, ticks_ms, ticks_diff

import config as C
from e3634a import E3634A
from bridge import Bridge
from ble_uart import BLEUART


def log(*a):
    if C.DEBUG:
        print(*a)


led = Pin(C.LED_PIN, Pin.OUT)


def led_set(on):
    led.value((not on) if C.LED_ACTIVE_LOW else on)


uart = UART(C.UART_ID, baudrate=C.BAUD, bits=C.BITS, parity=C.PARITY,
            stop=C.STOP, tx=C.UART_TX, rx=C.UART_RX, rxbuf=512, timeout=0)

if C.DSR_OUT_PIN is not None:
    Pin(C.DSR_OUT_PIN, Pin.OUT, value=1)   # ESP sempre pronto p/ receber
dtr_in = Pin(C.DTR_IN_PIN, Pin.IN) if C.DTR_IN_PIN is not None else None

psu = E3634A(uart, timeout_ms=C.TIMEOUT_MS, compound=C.COMPOUND_QUERY,
             dtr_in=dtr_in, log=log if C.DEBUG else None)
ble = BLEUART(C.BLE_NAME)
bridge = Bridge(psu, ble.send_line, poll_ms=C.POLL_MS, poll_min_ms=C.POLL_MIN_MS,
                reconnect_ms=C.RECONNECT_MS, lock_panel=C.LOCK_FRONT_PANEL)

print("E3634A-BLE pronto. BLE:", C.BLE_NAME)

t_led = ticks_ms()
led_on = False
while True:
    for connected in ble.events():
        log("BLE cliente", "conectado" if connected else "desconectado")
        bridge.on_client(connected)
    for line in ble.lines():
        log("APP>", line)
        bridge.handle_line(line)
    bridge.tick()

    # LED: aceso = app conectado e fonte ok; pisca rapido = app ok, fonte sem
    # resposta; pisca lento = aguardando app
    now = ticks_ms()
    if ble.connected and bridge.link:
        led_set(True)
    else:
        period = 150 if ble.connected else 1000
        if ticks_diff(now, t_led) >= period:
            t_led = now
            led_on = not led_on
            led_set(led_on)
    sleep_ms(5)
