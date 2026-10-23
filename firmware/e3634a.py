# Driver SCPI para a fonte Agilent/Keysight E3634A via RS-232.
# Compativel com MicroPython (machine.UART) e CPython (pyserial via adaptador
# ou simulador), pois so usa write()/read()/any() do objeto uart.
#
# Comunicacao (conforme o manual da familia E363x):
#   - a fonte e DTE e usa handshake DTR/DSR (hardware): ela so transmite com o
#     DSR (pino 6) verdadeiro; para desativar, ligar o DSR a nivel TRUE (jumper
#     4-6 no DB9). XON/XOFF nao faz parte do protocolo (suporte opcional aqui)
#   - a fonte segura o DTR em FALSO enquanto a saida esta suspensa; o travamento
#     (deadlock) se desfaz enviando <Ctrl-C> (0x03), que descarta a saida pendente
#   - por RS-232 e preciso mandar SYST:REM antes dos demais comandos
#   - escrita termina em "\r\n"; leitura aceita "\n" ou "\r"
#   - sessao: SYST:REM, *CLS, esvazia SYST:ERR? (max. 20), *IDN?
#     (a fila de erros vazia evita o beep da fonte)
#   - _write NAO limpa a entrada (evita -410 Query INTERRUPTED); so a query limpa
#   - medicao: MEAS:VOLT:DC?, 50 ms, MEAS:CURR:DC?

try:
    from time import ticks_ms, ticks_diff, sleep_ms
except ImportError:  # CPython
    import time as _t

    def ticks_ms():
        return int(_t.monotonic() * 1000)

    def ticks_diff(a, b):
        return a - b

    def sleep_ms(ms):
        _t.sleep(ms / 1000)


class LinkError(Exception):
    """Fonte nao respondeu (cabo, baud, handshake...)."""


class SCPIError(Exception):
    """Fonte reportou erro na fila SYST:ERR?."""

    def __init__(self, code, msg):
        Exception.__init__(self, "%d,%s" % (code, msg))
        self.code = code
        self.msg = msg


RANGES = {
    # faixa: (V max programavel, A max programavel)
    "P25V": (25.75, 7.21),
    "P50V": (51.5, 4.12),
}
OVP_MIN, OVP_MAX = 1.0, 55.0
OCP_MIN, OCP_MAX = 0.0, 7.5
MEM_SLOTS = 5


def _f(s):
    return float(s.strip())


def _b(s):
    s = s.strip().upper()
    return s in ("1", "ON")


class E3634A:
    def __init__(self, uart, timeout_ms=2500, compound=False, dtr_in=None, log=None,
                 xonxoff=True, state_every=5, activity=None, idle=None):
        self.uart = uart
        self.timeout_ms = timeout_ms
        self.compound = compound
        self.dtr_in = dtr_in  # objeto Pin (entrada) ou None
        self.log = log
        self.xonxoff = xonxoff
        self.activity = activity  # callback("tx"|"rx") p/ o LED de atividade serial
        self.idle = idle          # chamado enquanto espera (mantem o LED atualizado)
        self.state_every = state_every
        self._rx = b""
        self._paused = False      # fonte mandou XOFF: nao transmitir
        self._state = (False, False, False)
        self._state_n = 0
        self.idn = ""

    # ------------------------------------------------------------ baixo nivel
    def _dbg(self, *a):
        if self.log:
            self.log(*a)

    def _pull(self):
        """Move bytes da UART para _rx, tratando XON (0x11) / XOFF (0x13)."""
        n = self.uart.any()
        if not n:
            return False
        d = self.uart.read(n)
        if not d:
            return False
        if self.activity:
            self.activity("rx")
        if self.xonxoff:
            a, b = d.rfind(b"\x11"), d.rfind(b"\x13")
            if a >= 0 or b >= 0:
                self._paused = b > a
                d = d.replace(b"\x11", b"").replace(b"\x13", b"")
        self._rx += d
        return True

    def _flush_input(self):
        t0 = ticks_ms()
        while self._pull():
            if ticks_diff(ticks_ms(), t0) > 100:   # linha despejando dados sem parar
                break
        self._rx = b""

    def _send(self, line):
        if self.dtr_in is not None:
            t0 = ticks_ms()
            while not self.dtr_in.value():
                if ticks_diff(ticks_ms(), t0) > self.timeout_ms:
                    raise LinkError("DTR da fonte inativo")
                sleep_ms(2)
        if self.xonxoff:
            self._pull()                      # pega XOFF/XON ainda na fila da UART
        if self._paused:                      # XOFF recebido: esperar o XON
            t0 = ticks_ms()
            while self._paused:
                self._pull()
                if ticks_diff(ticks_ms(), t0) > self.timeout_ms:
                    raise LinkError("fonte em XOFF")
                sleep_ms(2)
        self._dbg(">>", line)
        self.uart.write(line.encode() + b"\r\n")
        if self.activity:
            self.activity("tx")

    def _readline(self, timeout_ms=None):
        timeout_ms = self.timeout_ms if timeout_ms is None else timeout_ms
        t0 = ticks_ms()
        while True:
            i = -1
            for k, c in enumerate(self._rx):   # primeiro \n ou \r
                if c == 10 or c == 13:
                    i = k
                    break
            if i >= 0:
                line, self._rx = self._rx[:i], self._rx[i + 1:]
                s = line.decode().strip(" \x00")
                if not s:                      # sobra do \r\n: ignorar
                    continue
                self._dbg("<<", s)
                return s
            # o tempo limite vale mesmo com dados chegando (fluxo continuo/ruido)
            if ticks_diff(ticks_ms(), t0) > timeout_ms:
                got, self._rx = self._rx, b""
                # bytes sem terminador: lixo = baud/paridade errados; vazio = sem resposta
                raise LinkError("timeout rx=" + (repr(got[:16]) if got else "vazio"))
            if self._pull():
                if len(self._rx) > 256:            # nao deixar crescer sem limite
                    self._rx = self._rx[-64:]
                continue
            if self.idle:
                self.idle()
            sleep_ms(2)

    def query(self, cmd, timeout_ms=None):
        self._flush_input()
        self._send(cmd)
        return self._readline(timeout_ms)

    def write(self, cmd, check=True):
        """Envia comando. Com check=True le SYST:ERR? (sincroniza e valida).

        Nao limpa a entrada: descartar uma resposta em transito faria a fonte
        acusar -410 Query INTERRUPTED e apitar. Quem limpa e a query()."""
        self._send(cmd)
        self._state_n = 0                   # estado em cache pode ter mudado
        if check:
            self.check_error()

    def read_error(self, timeout_ms=None):
        r = self.query("SYST:ERR?", timeout_ms)
        code, _, msg = r.partition(",")
        try:
            code = int(code)
        except ValueError:
            raise LinkError("resposta invalida: %r" % r)
        return code, msg.strip().strip('"')

    def errors(self, timeout_ms=None):
        out = []
        for _ in range(20):
            code, msg = self.read_error(timeout_ms)
            if code == 0:
                break
            out.append((code, msg))
        return out

    def check_error(self):
        errs = self.errors()
        if errs:
            raise SCPIError(errs[0][0], errs[0][1])

    # --------------------------------------------------------------- sessao
    def device_clear(self):
        """<Ctrl-C>: cancela a operacao em curso e descarta a saida pendente."""
        self.uart.write(b"\x03")
        if self.activity:
            self.activity("tx")
        sleep_ms(150)
        self._flush_input()

    def connect(self, lock_panel=False, labtest=False):
        self._paused = False
        self._flush_input()
        sleep_ms(300)                       # porta/fonte estabilizam
        if labtest:
            # sequencia do Lab Test (validada na fonte real): *CLS, esvazia SYST:ERR?,
            # *IDN?; so depois SYST:REM. Sem <Ctrl-C> e sem SYST:REM antes do *IDN?.
            self.write("*CLS", check=False)
            self.errors(1200)
            self.idn = self.query("*IDN?", 1200)
            if not self.idn:
                raise LinkError("IDN vazio")
            self.write("SYST:RWL" if lock_panel else "SYST:REM", check=False)
            sleep_ms(100)
            self._state_n = 0
            return self.idn
        self.device_clear()                 # desfaz travamento DTR/DSR de sessoes anteriores
        # RS-232 so aceita comandos em modo remoto: SYST:REM sempre primeiro
        self.write("SYST:RWL" if lock_panel else "SYST:REM", check=False)
        sleep_ms(100)
        self.write("*CLS", check=False)
        self.errors(1200)                   # esvazia a fila: sem beep
        self.idn = self.query("*IDN?", 1200)
        if not self.idn:
            raise LinkError("IDN vazio")
        self._state_n = 0                   # forca releitura do estado
        return self.idn

    def ping(self):
        return self.query("*IDN?")

    def reset(self):
        self.write("*RST")

    def clear(self):
        self.write("*CLS", check=False)

    def local(self):
        self.write("SYST:LOC", check=False)

    def remote(self, lock_panel=False):
        self.write("SYST:RWL" if lock_panel else "SYST:REM", check=False)

    def version(self):
        return self.query("SYST:VERS?")

    def beep(self):
        self.write("SYST:BEEP")

    # -------------------------------------------------------------- saida
    def output(self, on):
        self.write("OUTP %s" % ("ON" if on else "OFF"))

    def output_state(self):
        return _b(self.query("OUTP?"))

    def set_range(self, rng):
        rng = rng.upper()
        if rng in ("LOW", "P25"):
            rng = "P25V"
        elif rng in ("HIGH", "P50"):
            rng = "P50V"
        if rng not in RANGES:
            raise ValueError("faixa invalida")
        self.write("VOLT:RANG %s" % rng)

    def get_range(self):
        return self.query("VOLT:RANG?").strip().upper()

    def set_voltage(self, v):
        self.write("VOLT %.4f" % v)

    def set_current(self, a):
        self.write("CURR %.4f" % a)

    def apply(self, v, a):
        self.write("APPL %.4f,%.4f" % (v, a))

    def get_voltage(self):
        return _f(self.query("VOLT?"))

    def get_current(self):
        return _f(self.query("CURR?"))

    # ------------------------------------------------------------ medicao
    def measure_voltage(self):
        return _f(self.query("MEAS:VOLT:DC?"))

    def measure_current(self):
        return _f(self.query("MEAS:CURR:DC?"))

    def measure(self):
        """Retorna (V, I, saida_on, ovp_trip, ocp_trip)."""
        if self.compound:
            r = self.query(
                "MEAS:VOLT:DC?;:MEAS:CURR:DC?;:OUTP?;:VOLT:PROT:TRIP?;:CURR:PROT:TRIP?"
            )
            p = r.split(";")
            if len(p) != 5:
                raise LinkError("resposta composta invalida: %r" % r)
            return _f(p[0]), _f(p[1]), _b(p[2]), _b(p[3]), _b(p[4])
        v = self.measure_voltage()
        sleep_ms(50)
        i = self.measure_current()
        # saida/protecoes mudam raramente: reler a cada state_every leituras
        if self._state_n <= 0:
            self._state = (self.output_state(),
                           _b(self.query("VOLT:PROT:TRIP?")),
                           _b(self.query("CURR:PROT:TRIP?")))
            self._state_n = self.state_every
        self._state_n -= 1
        return (v, i) + self._state

    # ---------------------------------------------------------- protecoes
    def set_ovp(self, level=None, enabled=None):
        if level is not None:
            self.write("VOLT:PROT %.3f" % level)
        if enabled is not None:
            self.write("VOLT:PROT:STAT %s" % ("ON" if enabled else "OFF"))

    def set_ocp(self, level=None, enabled=None):
        if level is not None:
            self.write("CURR:PROT %.4f" % level)
        if enabled is not None:
            self.write("CURR:PROT:STAT %s" % ("ON" if enabled else "OFF"))

    def clear_protection(self):
        # A causa (sobretensao/sobrecorrente) deve ter sido removida antes.
        self.write("VOLT:PROT:CLE")
        self.write("CURR:PROT:CLE")

    # ----------------------------------------------------------- memorias
    def save(self, n):
        if not 1 <= n <= MEM_SLOTS:
            raise ValueError("memoria 1..5")
        self.write("*SAV %d" % n)

    def recall(self, n):
        if not 1 <= n <= MEM_SLOTS:
            raise ValueError("memoria 1..5")
        self.write("*RCL %d" % n)

    # ------------------------------------------------------------ display
    def display(self, on):
        self.write("DISP %s" % ("ON" if on else "OFF"))

    def display_text(self, text):
        text = text.replace('"', "'")[:12]
        self.write('DISP:TEXT "%s"' % text)

    def display_text_clear(self):
        self.write("DISP:TEXT:CLE")

    # ------------------------------------------------------------ trigger
    def trigger_source(self, src):
        src = src.upper()
        if src not in ("BUS", "IMM"):
            raise ValueError("fonte de trigger BUS|IMM")
        self.write("TRIG:SOUR %s" % src)

    def trigger_delay(self, s):
        self.write("TRIG:DEL %d" % int(s))

    def trigger_levels(self, v=None, a=None):
        if v is not None:
            self.write("VOLT:TRIG %.4f" % v)
        if a is not None:
            self.write("CURR:TRIG %.4f" % a)

    def initiate(self):
        self.write("INIT")

    def trigger(self):
        self.write("*TRG")

    # ------------------------------------------------------------- estado
    def status(self):
        """Le toda a configuracao programada da fonte."""
        return {
            "range": self.get_range(),
            "vset": self.get_voltage(),
            "iset": self.get_current(),
            "out": self.output_state(),
            "ovp": _f(self.query("VOLT:PROT?")),
            "ovp_en": _b(self.query("VOLT:PROT:STAT?")),
            "ocp": _f(self.query("CURR:PROT?")),
            "ocp_en": _b(self.query("CURR:PROT:STAT?")),
            "disp": _b(self.query("DISP?")),
            "trig_src": self.query("TRIG:SOUR?").strip().upper(),
            "trig_del": _f(self.query("TRIG:DEL?")),
            "vtrig": _f(self.query("VOLT:TRIG?")),
            "itrig": _f(self.query("CURR:TRIG?")),
        }
