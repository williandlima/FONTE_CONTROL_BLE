# Periferico BLE com Nordic UART Service (NUS) para MicroPython.
# RX (app -> ESP): escrita na caracteristica 6E400002
# TX (ESP -> app): notificacao na caracteristica 6E400003

import bluetooth
import struct
from micropython import const
from time import sleep_ms

_IRQ_CENTRAL_CONNECT = const(1)
_IRQ_CENTRAL_DISCONNECT = const(2)
_IRQ_GATTS_WRITE = const(3)
_IRQ_MTU_EXCHANGED = const(21)

_FLAG_READ = const(0x0002)
_FLAG_WRITE_NO_RESPONSE = const(0x0004)
_FLAG_WRITE = const(0x0008)
_FLAG_NOTIFY = const(0x0010)

_UART_UUID = bluetooth.UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
_UART_TX = (bluetooth.UUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"),
            _FLAG_READ | _FLAG_NOTIFY)
_UART_RX = (bluetooth.UUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"),
            _FLAG_WRITE | _FLAG_WRITE_NO_RESPONSE)
_UART_SERVICE = (_UART_UUID, (_UART_TX, _UART_RX))

_ADV_TYPE_FLAGS = const(0x01)
_ADV_TYPE_NAME = const(0x09)
_ADV_TYPE_UUID128_COMPLETE = const(0x07)


def _adv_field(t, value):
    return struct.pack("BB", len(value) + 1, t) + value


def _adv_payload(name):
    return _adv_field(_ADV_TYPE_FLAGS, b"\x06") + _adv_field(_ADV_TYPE_NAME, name.encode())


def _resp_payload():
    return _adv_field(_ADV_TYPE_UUID128_COMPLETE, bytes(_UART_UUID))


class BLEUART:
    def __init__(self, name="E3634A-BLE", rxbuf=512, on_connect=None):
        self._ble = bluetooth.BLE()
        self._ble.active(True)
        try:
            self._ble.config(gap_name=name, mtu=247)
        except Exception:
            pass
        self._ble.irq(self._irq)
        ((self._tx, self._rx),) = self._ble.gatts_register_services((_UART_SERVICE,))
        self._ble.gatts_set_buffer(self._rx, rxbuf, True)
        self._conn = None
        self._mtu = 23
        self._inbuf = b""
        self._lines = []
        self._events = []           # ("c", True/False) - tratado no loop
        self._name = name
        self._adv = _adv_payload(name)
        self._resp = _resp_payload()
        self.advertise()

    def _irq(self, event, data):
        # Executa em contexto de IRQ: nada de UART/JSON aqui, so enfileira.
        if event == _IRQ_CENTRAL_CONNECT:
            self._conn = data[0]
            self._mtu = 23
            self._events.append(True)
        elif event == _IRQ_CENTRAL_DISCONNECT:
            self._conn = None
            self._inbuf = b""
            self._events.append(False)
            self.advertise()
        elif event == _IRQ_GATTS_WRITE:
            conn, handle = data
            if handle == self._rx:
                self._inbuf += self._ble.gatts_read(self._rx)
                while True:
                    i = self._inbuf.find(b"\n")
                    if i < 0:
                        break
                    self._lines.append(self._inbuf[:i])
                    self._inbuf = self._inbuf[i + 1:]
                if len(self._inbuf) > 1024:   # protecao contra lixo sem '\n'
                    self._inbuf = b""
        elif event == _IRQ_MTU_EXCHANGED:
            self._mtu = data[1]

    def advertise(self, interval_us=200000):
        self._ble.gap_advertise(interval_us, adv_data=self._adv, resp_data=self._resp)

    @property
    def connected(self):
        return self._conn is not None

    def events(self):
        ev, self._events = self._events, []
        return ev

    def lines(self):
        ls, self._lines = self._lines, []
        return [l.decode() for l in ls]

    def send_line(self, s):
        if self._conn is None:
            return
        data = s.encode() + b"\n"
        step = max(20, self._mtu - 3)
        for i in range(0, len(data), step):
            chunk = data[i:i + step]
            for _ in range(20):
                try:
                    self._ble.gatts_notify(self._conn, self._tx, chunk)
                    break
                except OSError:      # fila de notificacoes cheia
                    sleep_ms(5)
                    if self._conn is None:
                        return
