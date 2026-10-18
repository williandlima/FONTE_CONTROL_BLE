# Autoteste de fiacao RS-232. Rode no REPL do ESP32-C3:
#
#   import selftest
#   selftest.loopback()   # 1) jumper nos pinos 2-3 do DB9 do modulo
#   selftest.probe()      # 2) modulo ligado na fonte (cabo final)
#
# Nao depende do BLE nem do bridge; usa so a UART.

from machine import UART
from time import sleep_ms, ticks_ms, ticks_diff

import config as C

TX, RX = C.UART_TX, C.UART_RX


def _uart(tx, rx, baud=None, stop=1):
    return UART(C.UART_ID, baudrate=baud or C.BAUD, bits=C.BITS, parity=C.PARITY,
                stop=stop, tx=tx, rx=rx, rxbuf=256, timeout=0)


def _drain(u):
    while u.any():
        u.read()


def loopback():
    """Com um fio ligando os pinos 2 e 3 do DB9 do MODULO (sem a fonte), o que
    o ESP transmite deve voltar. Testa os dois sentidos dos fios TX/RX."""
    found = None
    for tx, rx in ((TX, RX), (RX, TX)):
        u = _uart(tx, rx)
        _drain(u)
        u.write(b"LOOP-TEST\n")
        sleep_ms(150)
        d = u.read(u.any()) if u.any() else b""
        u.deinit()
        ok = d == b"LOOP-TEST\n"
        print("TX=GPIO%d RX=GPIO%d -> %s %r" % (tx, rx, "OK" if ok else "falhou", d))
        if ok and found is None:
            found = (tx, rx)
    if found is None:
        print("Nenhum eco: confira VCC=3V3, GND, o jumper 2-3 no DB9 e se o chip e "
              "um MAX3232 (o LED D1 do modulo deve acender).")
    elif found == (TX, RX):
        print("Fiacao correta como no config.py.")
    else:
        print("Fios invertidos: ligue o TX do modulo no GPIO%d e o RX no GPIO%d "
              "(ou troque UART_TX/UART_RX no config.py)." % (found[1], found[0]))
    return found


def _read_for(u, ms):
    t0 = ticks_ms()
    buf = b""
    while ticks_diff(ticks_ms(), t0) < ms:
        if u.any():
            buf += u.read(u.any())
        else:
            sleep_ms(5)
    return buf


def probe(bauds=(9600, 19200, 4800, 2400, 1200), stops=(2, 1)):
    """Com a fonte ligada, procura baud/stop/sentido de TX-RX em que ela responde."""
    for tx, rx in ((TX, RX), (RX, TX)):
        for baud in bauds:
            for stop in stops:
                u = _uart(tx, rx, baud, stop)
                _drain(u)
                u.write(b"\n")
                sleep_ms(50)
                u.write(b"SYST:REM\n")
                sleep_ms(100)
                u.write(b"*IDN?\n")
                d = _read_for(u, 700)
                u.deinit()
                print("TX=%d RX=%d baud=%d stop=%d -> %r" % (tx, rx, baud, stop, d))
                if b"HEWLETT" in d or b"AGILENT" in d.upper() or b"E36" in d:
                    print("\nACHOU: ajuste config.py -> UART_TX=%d UART_RX=%d BAUD=%d STOP=%d"
                          % (tx, rx, baud, stop))
                    return tx, rx, baud, stop
    print("\nSem resposta. Verifique: jumper 4-6 na fonte, cabo null-modem "
          "(2<->3 cruzado), e se a fonte esta em RS-232 (nao HPIB).")
    return None
