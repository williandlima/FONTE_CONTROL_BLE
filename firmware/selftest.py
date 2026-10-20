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


def probe(bauds=(9600, 19200, 4800, 2400, 1200), stops=(1, 2)):
    """Com a fonte ligada, procura baud/stop/sentido de TX-RX em que ela responde."""
    for tx, rx in ((TX, RX), (RX, TX)):
        for baud in bauds:
            for stop in stops:
                u = _uart(tx, rx, baud, stop)
                _drain(u)
                sleep_ms(300)
                u.write(b"*CLS\r\n")
                sleep_ms(100)
                u.write(b"*IDN?\r\n")
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


def _rx_idle(samples=200, ms=2):
    from machine import Pin
    p = Pin(RX, Pin.IN)
    hi = 0
    for _ in range(samples):
        hi += p.value()
        sleep_ms(ms)
    return hi, samples


def beep():
    """Testa so o caminho ESP -> fonte: manda SYST:REM e SYST:BEEP.

    Se a fonte mostrar RMT e apitar UMA vez, os comandos chegam e o problema esta
    no caminho de volta (fonte -> ESP). Se nada acontecer, o problema e na ida."""
    u = _uart(TX, RX)
    _drain(u)
    u.write(b"SYST:REM\r\n")
    sleep_ms(400)
    u.write(b"SYST:BEEP\r\n")
    sleep_ms(400)
    u.deinit()
    print("Enviado. Olhe a fonte: RMT no display? apitou uma vez?")


def diag():
    """Diagnostico completo da serial com a fonte ligada (pare o main com Stop)."""
    print("== 1) nivel ocioso do RX (GPIO%d), esperado ALTO ==" % RX)
    hi, n = _rx_idle()
    print("   alto em %d de %d leituras" % (hi, n))
    if hi == n:
        print("   OK")
    elif hi == 0:
        print("   BAIXO fixo: modulo sem 3V3/GND, fio do RX solto ou linha da fonte em break")
    else:
        print("   oscilando: chegam dados/ruido sem pedir nada")

    print("== 2) escuta passiva de 1,5 s ==")
    u = _uart(TX, RX)
    _drain(u)
    got = _read_for(u, 1500)
    u.deinit()
    print("   recebido:", got if got else "nada")

    print("== 3) *IDN? em varias configuracoes ==")
    best = None
    seen = False
    combos = []
    for tx, rx in ((TX, RX), (RX, TX)):
        for stop in (1, 2):
            for eol in (b"\r\n", b"\n"):
                combos.append((tx, rx, 9600, stop, eol))
    for baud in (19200, 4800, 2400, 1200):
        combos.append((TX, RX, baud, 1, b"\r\n"))
    for tx, rx, baud, stop, eol in combos:
        u = _uart(tx, rx, baud, stop)
        _drain(u)
        sleep_ms(200)
        u.write(b"*CLS" + eol)
        sleep_ms(100)
        u.write(b"*IDN?" + eol)
        d = _read_for(u, 800)
        u.deinit()
        if d:
            seen = True
        print("   TX=%d RX=%d %d baud stop=%d eol=%r -> %r" % (tx, rx, baud, stop, eol, d))
        if b"HEWLETT" in d or b"AGILENT" in d.upper() or b"E36" in d:
            best = (tx, rx, baud, stop, eol)
            break

    print("== resultado ==")
    if best:
        print("A fonte respondeu. Use no config.py: UART_TX=%d UART_RX=%d BAUD=%d STOP=%d (terminador %r)" % best)
    elif seen:
        print("A fonte devolve bytes, mas nao um *IDN? valido: velocidade, paridade ou "
              "formato diferentes. Confira I/O Config da fonte (RS-232, 9600, NONE 8 BITS).")
    else:
        print("Mudo em todas as combinacoes. Rode selftest.beep(): se a fonte mostrar RMT/apitar, "
              "falta so o caminho de volta (pino 3 da fonte, GND, fio do RX, handshake). "
              "Se nao reagir, falta o caminho de ida (cabo cruzado 2<->3, fio do TX).")
    return best
