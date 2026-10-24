"""Simulador simples da E3634A na interface RS-232 (objeto tipo UART).

Implementa o subconjunto SCPI usado pelo firmware, com uma carga resistiva
para gerar leituras de V/I plausiveis (CV/CC). Serve para testes unitarios e
para desenvolver sem a fonte na bancada.
"""

IDN = "HEWLETT-PACKARD,E3634A,0,2.4-6.1-2.1"

RANGES = {"P25V": (25.75, 7.21), "P50V": (51.5, 4.12)}


class SimE3634A:
    def __init__(self, load_ohm=10.0, compound_ok=True, flow=False):
        self.load = load_ohm
        self.compound_ok = compound_ok
        self.flow = flow          # responde envolvendo XOFF/XON, como a fonte real
        self.alive = True
        self.remote = False
        self._in = b""
        self._out = b""
        self.log = []
        self.mem = {}
        self.text = None
        self.reset()

    # --- interface UART -------------------------------------------------
    def write(self, data):
        if not self.alive:
            return len(data)
        if b"\x03" in data:                      # device clear
            data = data.replace(b"\x03", b"")
            self._out = b""
            self.cleared = getattr(self, "cleared", 0) + 1
        self._in += data
        while b"\n" in self._in:
            line, self._in = self._in.split(b"\n", 1)
            line = line.decode().strip()
            if line:
                self.log.append(line)
                self._execute(line)
        return len(data)

    def any(self):
        return len(self._out)

    def read(self, n=-1):
        if n is None or n < 0:
            n = len(self._out)
        d, self._out = self._out[:n], self._out[n:]
        return d

    # --- estado -----------------------------------------------------------
    def reset(self):
        self.range = "P25V"
        self.vset = 0.0
        self.iset = 7.0
        self.out = False
        self.ovp, self.ovp_en, self.ovp_trip = 55.0, False, False
        self.ocp, self.ocp_en, self.ocp_trip = 7.5, False, False
        self.disp = True
        self.trig_src, self.trig_del = "IMM", 0.0
        self.vtrig, self.itrig = 0.0, 7.0
        self.errq = []

    def _snap(self):
        return dict((k, getattr(self, k)) for k in (
            "range", "vset", "iset", "ovp", "ovp_en", "ocp", "ocp_en"))

    def _err(self, code, msg):
        self.errq.append((code, msg))

    def _vi(self):
        if not self.out or self.ovp_trip or self.ocp_trip:
            return 0.0, 0.0
        i = self.vset / self.load
        if i <= self.iset:
            v = self.vset
        else:
            i = self.iset
            v = i * self.load
        if self.ovp_en and v > self.ovp:
            self.ovp_trip, self.out = True, False
            return 0.0, 0.0
        if self.ocp_en and i > self.ocp:
            self.ocp_trip, self.out = True, False
            return 0.0, 0.0
        return round(v, 4), round(i, 5)

    # --- SCPI ---------------------------------------------------------------
    def _execute(self, line):
        answers = []
        for part in line.split(";"):
            part = part.strip()
            if part.startswith(":"):
                part = part[1:]
            if not part:
                continue
            if part.endswith("?") and len(answers) and not self.compound_ok:
                self._err(-410, "Query INTERRUPTED")
                continue
            try:
                a = self._one(part)
            except ValueError:
                self._err(-222, "Data out of range")
                a = None
            if a is not None:
                answers.append(a)
        if answers:
            resp = (";".join(answers) + "\r\n").encode()
            if self.flow:
                resp = b"\x13" + resp[:3] + b"\x11" + resp[3:]
            self._out += resp

    @staticmethod
    def _onoff(arg):
        a = arg.upper()
        if a in ("ON", "1"):
            return True
        if a in ("OFF", "0"):
            return False
        raise ValueError(arg)

    def _one(self, p):
        head, _, arg = p.partition(" ")
        h = head.upper()
        arg = arg.strip()
        fe = lambda x: "%+.8E" % x
        vmax, imax = RANGES[self.range]

        if h == "*IDN?":
            return IDN
        if h == "*RST":
            self.reset()
            return None
        if h == "*CLS":
            self.errq = []
            return None
        if h == "*SAV":
            self.mem[int(arg)] = self._snap()
            return None
        if h == "*RCL":
            n = int(arg)
            if n not in self.mem:
                self.reset()
            else:
                for k, v in self.mem[n].items():
                    setattr(self, k, v)
            self.out = False
            return None
        if h == "*TRG":
            if self.trig_src == "BUS":
                self.vset, self.iset = self.vtrig, self.itrig
            return None
        if h == "*OPC?":
            return "1"
        if h in ("SYST:REM", "SYST:RWL"):
            self.remote = True
            return None
        if h == "SYST:LOC":
            self.remote = False
            return None
        if h == "SYST:ERR?":
            if self.errq:
                c, m = self.errq.pop(0)
                return '%+d,"%s"' % (c, m)
            return '+0,"No error"'
        if not self.remote:
            self._err(-221, "Settings conflict")   # comportamento sem SYST:REM
            return None
        if h == "SYST:VERS?":
            return "1994.0"
        if h == "SYST:BEEP":
            return None
        if h in ("VOLT", "VOLT:LEV"):
            v = float(arg)
            if not 0 <= v <= vmax:
                raise ValueError
            self.vset = v
            return None
        if h in ("CURR", "CURR:LEV"):
            a = float(arg)
            if not 0 <= a <= imax:
                raise ValueError
            self.iset = a
            return None
        if h in ("APPL", "APPLY"):
            v, a = [float(x) for x in arg.split(",")]
            if not (0 <= v <= vmax and 0 <= a <= imax):
                raise ValueError
            self.vset, self.iset = v, a
            return None
        if h == "VOLT?":
            return fe(self.vset)
        if h == "CURR?":
            return fe(self.iset)
        if h == "VOLT:RANG":
            r = arg.upper()
            r = {"LOW": "P25V", "HIGH": "P50V"}.get(r, r)
            if r not in RANGES:
                raise ValueError
            self.range = r
            vmax, imax = RANGES[r]
            self.vset = min(self.vset, vmax)
            self.iset = min(self.iset, imax)
            return None
        if h == "VOLT:RANG?":
            return self.range
        if h in ("OUTP", "OUTP:STAT"):
            self.out = self._onoff(arg)
            return None
        if h == "OUTP?":
            return "1" if self.out else "0"
        if h in ("MEAS:VOLT?", "MEAS:VOLT:DC?"):
            return fe(self._vi()[0])
        if h in ("MEAS:CURR?", "MEAS:CURR:DC?"):
            return fe(self._vi()[1])
        if h == "VOLT:PROT":
            self.ovp = float(arg)
            return None
        if h == "VOLT:PROT?":
            return fe(self.ovp)
        if h == "VOLT:PROT:STAT":
            self.ovp_en = self._onoff(arg)
            return None
        if h == "VOLT:PROT:STAT?":
            return "1" if self.ovp_en else "0"
        if h == "VOLT:PROT:TRIP?":
            return "1" if self.ovp_trip else "0"
        if h == "VOLT:PROT:CLE":
            self.ovp_trip = False
            return None
        if h == "CURR:PROT":
            self.ocp = float(arg)
            return None
        if h == "CURR:PROT?":
            return fe(self.ocp)
        if h == "CURR:PROT:STAT":
            self.ocp_en = self._onoff(arg)
            return None
        if h == "CURR:PROT:STAT?":
            return "1" if self.ocp_en else "0"
        if h == "CURR:PROT:TRIP?":
            return "1" if self.ocp_trip else "0"
        if h == "CURR:PROT:CLE":
            self.ocp_trip = False
            return None
        if h == "DISP":
            self.disp = self._onoff(arg)
            return None
        if h == "DISP?":
            return "1" if self.disp else "0"
        if h == "DISP:TEXT":
            self.text = arg.strip('"')
            return None
        if h == "DISP:TEXT:CLE":
            self.text = None
            return None
        if h == "TRIG:SOUR":
            self.trig_src = arg.upper()
            return None
        if h == "TRIG:SOUR?":
            return self.trig_src
        if h == "TRIG:DEL":
            self.trig_del = float(arg)
            return None
        if h == "TRIG:DEL?":
            return "%+d" % self.trig_del
        if h == "VOLT:TRIG":
            self.vtrig = float(arg)
            return None
        if h == "VOLT:TRIG?":
            return fe(self.vtrig)
        if h == "CURR:TRIG":
            self.itrig = float(arg)
            return None
        if h == "CURR:TRIG?":
            return fe(self.itrig)
        if h == "INIT":
            if self.trig_src == "IMM":
                self.vset, self.iset = self.vtrig, self.itrig
            return None
        self._err(-113, "Undefined header")
        return None
