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


class _LedUART:
    """UART que pisca o LED azul: flash curto (40 ms) ao enviar, longo (200 ms) ao receber."""

    def __init__(self, uart):
        from machine import Pin
        self._u = uart
        self._led = Pin(C.LED_PIN, Pin.OUT)
        self._flash(0)

    def _flash(self, ms):
        on = ms > 0
        self._led.value((not on) if C.LED_ACTIVE_LOW else on)
        if on:
            sleep_ms(ms)
            self._led.value(1 if C.LED_ACTIVE_LOW else 0)

    def write(self, data):
        n = self._u.write(data)
        self._flash(40)
        return n

    def any(self):
        return self._u.any()

    def read(self, n=-1):
        d = self._u.read(n)
        if d:
            self._flash(200)
        return d

    def deinit(self):
        self._u.deinit()


def _uart(tx, rx, baud=None, stop=1, bits=None, parity=-1):
    return _LedUART(UART(C.UART_ID, baudrate=baud or C.BAUD, bits=bits or C.BITS,
                         parity=C.PARITY if parity == -1 else parity,
                         stop=stop, tx=tx, rx=rx, rxbuf=256, timeout=0))


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
                u.write(b"\x03")                 # device clear (Ctrl-C)
                sleep_ms(150)
                u.write(b"SYST:REM\r\n")        # RS-232 exige modo remoto
                sleep_ms(150)
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
    print("\nSem resposta. Verifique: pino 6 (DSR) em +5 V por 1 kOhm, pino 4 solto (sem "
          "jumper 4-6), fonte religada e em RS-232 (nao HPIB).")
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
    u.write(b"\x03")                         # device clear (Ctrl-C)
    sleep_ms(150)
    u.write(b"SYST:REM\r\n")
    sleep_ms(400)
    u.write(b"SYST:BEEP\r\n")
    sleep_ms(400)
    u.deinit()
    print("Enviado. Olhe a fonte: RMT no display? apitou uma vez?")


def labtest():
    """Reproduz a abertura do Lab Test, byte a byte: 9600 8N1, espera 0,3 s, *CLS,
    esvazia SYST:ERR?, *IDN?. Sem Ctrl-C e sem SYST:REM. Imprime o que a fonte devolve."""
    u = _uart(TX, RX, 9600, 1, 8, None)
    _drain(u)
    sleep_ms(300)

    def ask(cmd, ms=2000):
        u.write(cmd + b"\r\n")
        buf = b""
        t0 = ticks_ms()
        while ticks_diff(ticks_ms(), t0) < ms:
            if u.any():
                buf += u.read(u.any())
                if b"\n" in buf:
                    break
            else:
                sleep_ms(5)
        print("%-12r -> %r" % (cmd, buf))
        return buf

    u.write(b"*CLS\r\n")
    sleep_ms(100)
    for _ in range(5):
        r = ask(b"SYST:ERR?")
        if not r or b"+0" in r:
            break
    r = ask(b"*IDN?", 3000)
    u.deinit()
    if b"E36" in r or b"HEWLETT" in r.upper() or b"AGILENT" in r.upper():
        print("OK: a fonte respondeu com a sequencia do Lab Test.")
    else:
        print("Sem resposta com a sequencia exata do Lab Test: o metodo nao e a causa; "
              "a diferenca esta na camada eletrica (DSR/DTR, pino 3, nivel).")
    return r


def _edges(ms):
    """Conta transicoes no pino RX durante ms (qualquer atividade, mesmo sem decodificar)."""
    from machine import Pin
    p = Pin(RX, Pin.IN)
    last = p.value()
    n = 0
    t0 = ticks_ms()
    while ticks_diff(ticks_ms(), t0) < ms:
        v = p.value()
        if v != last:
            n += 1
            last = v
    return n, last


def edges():
    """Detector de atividade no RX, independente de baud/paridade/decodificacao.

    Manda Ctrl-C, SYST:REM e *IDN? so pelo TX e conta as transicoes no RX.
    > 0 depois do comando: a fonte transmite (se o diag nao decodifica, e baud/formato).
    0 sempre: nada chega ao pino RX (fonte nao transmite, DSR falso, pino 3 do cabo, R1IN)."""
    u = UART(C.UART_ID, baudrate=C.BAUD, bits=C.BITS, parity=C.PARITY,
             stop=1, tx=TX, timeout=0)
    sleep_ms(300)
    idle, lvl = _edges(1000)
    print("ocioso: %d transicoes, nivel final %d (esperado 0 e 1)" % (idle, lvl))
    u.write(b"\x03")
    sleep_ms(150)
    u.write(b"SYST:REM\r\n")
    sleep_ms(200)
    u.write(b"*IDN?\r\n")
    act, lvl = _edges(1500)
    u.deinit()
    print("apos *IDN?: %d transicoes, nivel final %d" % (act, lvl))
    if act > 4:
        print("HA ATIVIDADE no RX: a fonte transmite. Se diag() nao decodifica, "
              "o problema e baud/paridade/bits ou nivel; veja a fila de erros da fonte.")
    else:
        print("NADA no RX: a fonte nao esta transmitindo ate o pino. Suspeitos: DSR falso, "
              "fio do pino 3 do cabo, R1IN do modulo, fonte em HPIB ou travada (religar).")
    return act


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
                combos.append((tx, rx, 9600, stop, eol, 8, None))
    for bits, par in ((7, 0), (7, 1)):          # opcoes "Even 7 bits" e "Odd 7 bits"
        for eol in (b"\r\n", b"\n"):
            combos.append((TX, RX, 9600, 1, eol, bits, par))
    for baud in (19200, 4800, 2400, 1200):
        combos.append((TX, RX, baud, 1, b"\r\n", 8, None))
    for tx, rx, baud, stop, eol, bits, par in combos:
        u = _uart(tx, rx, baud, stop, bits, par)
        _drain(u)
        sleep_ms(200)
        u.write(b"\x03")                         # device clear (Ctrl-C)
        sleep_ms(150)
        u.write(b"SYST:REM" + eol)               # RS-232 exige modo remoto
        sleep_ms(150)
        u.write(b"*CLS" + eol)
        sleep_ms(100)
        u.write(b"*IDN?" + eol)
        d = _read_for(u, 800)
        u.deinit()
        if d:
            seen = True
        print("   TX=%d RX=%d %d baud %d bits par=%s stop=%d eol=%r -> %r"
              % (tx, rx, baud, bits, par, stop, eol, d))
        if b"HEWLETT" in d or b"AGILENT" in d.upper() or b"E36" in d:
            best = (tx, rx, baud, stop, bits, par, eol)
            break

    print("== resultado ==")
    if best:
        print("A fonte respondeu. Use no config.py: UART_TX=%d UART_RX=%d BAUD=%d STOP=%d "
              "BITS=%d PARITY=%s (terminador %r)" % best)
    elif seen:
        print("A fonte devolve bytes, mas nao um *IDN? valido: velocidade, paridade ou "
              "formato diferentes. Confira I/O Config da fonte (RS-232, 9600, NONE 8 BITS).")
    else:
        print("Mudo em todas as combinacoes. Rode selftest.beep(): se a fonte mostrar RMT/apitar, "
              "falta so o caminho de volta (pino 3 da fonte, GND, fio do RX, handshake). "
              "Se nao reagir, falta o caminho de ida (cabo cruzado 2<->3, fio do TX).")
    return best
