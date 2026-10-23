# Protocolo app <-> firmware: uma mensagem JSON por linha ('\n').
# Ver docs/PROTOCOLO.md para a especificacao completa.

import json

from e3634a import LinkError, SCPIError, RANGES, OVP_MIN, OVP_MAX, OCP_MIN, OCP_MAX

try:
    from time import ticks_ms, ticks_diff
except ImportError:
    from e3634a import ticks_ms, ticks_diff


FW_VERSION = "1.1.3"   # sobe a cada mudanca de firmware; aparece no app


class CmdError(Exception):
    pass


def _num(d, k, lo=None, hi=None):
    if k not in d:
        raise CmdError("campo '%s' ausente" % k)
    try:
        x = float(d[k])
    except (TypeError, ValueError):
        raise CmdError("campo '%s' invalido" % k)
    if (lo is not None and x < lo) or (hi is not None and x > hi):
        raise CmdError("%s fora da faixa (%s..%s)" % (k, lo, hi))
    return x


class Bridge:
    def __init__(self, psu, send, poll_ms=500, poll_min_ms=150,
                 reconnect_ms=3000, lock_panel=False, labtest=False):
        self.psu = psu
        self.send = send            # send(str) -> envia uma linha ao app
        self.poll_ms = poll_ms
        self.poll_min_ms = poll_min_ms
        self.reconnect_ms = reconnect_ms
        self.lock_panel = lock_panel
        self.labtest = labtest
        self.link = False
        self.client = False         # app conectado via BLE
        self.state = {}
        self._t_poll = 0
        self._t_link = None
        self._need_status = False
        self._fails = 0
        self._errs = 0              # falhas seguidas com o link ativo
        self.last_err = ""

    # -------------------------------------------------------------- saida
    def emit(self, obj):
        self.send(json.dumps(obj))

    def _set_link(self, ok, reason=None):
        if ok != self.link:
            self.link = ok
            m = {"t": "link", "ok": ok}
            if ok:
                m["idn"] = self.psu.idn
            elif reason:
                m["err"] = reason
            self.emit(m)

    # ------------------------------------------------------------ eventos
    def on_client(self, connected):
        self.client = connected
        if connected:
            self._t_link = None  # tenta (re)ligar a fonte imediatamente
            m = {"t": "hello", "fw": FW_VERSION, "link": self.link, "poll": self.poll_ms}
            if self.link:
                m["idn"] = self.psu.idn
            self.emit(m)
            self._need_status = self.link

    def _retry_ms(self):
        # recuo progressivo (3s, 6s, 12s, ... ate 30s): cada tentativa com a
        # serial errada faz a fonte apitar, entao nao insistir a cada 3 s
        return min(self.reconnect_ms << min(self._fails, 4), max(self.reconnect_ms, 30000))

    def _link_error(self, e, now=None):
        """Falha transitoria nao derruba o link: so 3 seguidas marcam erro."""
        self._errs += 1
        if self._errs >= 3:
            self._errs = 0
            self._fails = 0
            self.last_err = str(e)
            self._set_link(False, str(e))
            self._t_link = ticks_ms() if now is None else now

    def try_link(self):
        try:
            self.psu.connect(self.lock_panel, self.labtest)
        except (LinkError, SCPIError) as e:
            self._fails += 1
            self.last_err = str(e)
            self._set_link(False, str(e))
            return False
        self._fails = 0
        self._errs = 0
        self.link = False
        self._set_link(True)
        self._need_status = True
        return True

    def tick(self):
        """Chamar periodicamente no loop principal."""
        now = ticks_ms()
        if not self.link:
            if self._t_link is None or ticks_diff(now, self._t_link) >= self._retry_ms():
                self._t_link = now
                self.try_link()
            return
        if not self.client:
            return
        try:
            if self._need_status:
                self._need_status = False
                self.send_status()
            if self.poll_ms and ticks_diff(now, self._t_poll) >= self.poll_ms:
                self._t_poll = now
                self.send_measure()
        except LinkError as e:
            self._link_error(e, now)
        except SCPIError as e:
            self.emit({"t": "e", "code": e.code, "msg": e.msg})

    def send_measure(self):
        v, i, out, ovp_t, ocp_t = self.psu.measure()
        self._errs = 0
        self.emit({"t": "m", "v": v, "i": i, "o": out,
                   "ovp_t": ovp_t, "ocp_t": ocp_t})

    def send_status(self):
        st = self.psu.status()
        self.state = st
        st = dict(st)
        st["t"] = "st"
        st["poll"] = self.poll_ms
        self.emit(st)

    # ------------------------------------------------------------ comandos
    def handle_line(self, line):
        line = line.strip()
        if not line:
            return
        rid = None
        try:
            try:
                d = json.loads(line)
            except ValueError:
                raise CmdError("JSON invalido")
            if not isinstance(d, dict) or "cmd" not in d:
                raise CmdError("faltando 'cmd'")
            rid = d.get("id")
            data = self._dispatch(d["cmd"], d)
            r = {"t": "r", "ok": True, "cmd": d["cmd"]}
            if data is not None:
                r["d"] = data
        except CmdError as e:
            r = {"t": "r", "ok": False, "err": str(e)}
        except ValueError as e:
            r = {"t": "r", "ok": False, "err": str(e)}
        except SCPIError as e:
            r = {"t": "r", "ok": False, "err": "SCPI %d: %s" % (e.code, e.msg),
                 "code": e.code}
            self._need_status = True
        except LinkError as e:
            self._link_error(e)
            r = {"t": "r", "ok": False, "err": "sem resposta da fonte"}
        if rid is not None:
            r["id"] = rid
        self.emit(r)

    def _require_link(self):
        if not self.link:
            raise CmdError("fonte desconectada")

    def _lim(self):
        # faixa desconhecida: limites mais largos; a fonte valida o resto
        return RANGES.get(self.state.get("range"),
                          (RANGES["P50V"][0], RANGES["P25V"][1]))

    def _dispatch(self, c, d):
        p = self.psu
        # comandos locais (nao precisam da fonte)
        if c == "poll":
            ms = int(_num(d, "ms", 0, 60000))
            self.poll_ms = 0 if ms == 0 else max(ms, self.poll_min_ms)
            return self.poll_ms
        if c == "ping":
            return "pong"
        if c == "version":
            return FW_VERSION
        if c == "link":
            if not self.try_link():
                raise CmdError("sem resposta da fonte: " + self.last_err)
            return True

        self._require_link()
        vmax, imax = self._lim()
        changed = True
        res = None

        if c == "out":
            p.output(bool(d.get("on")))
        elif c == "volt":
            p.set_voltage(_num(d, "v", 0, vmax))
        elif c == "curr":
            p.set_current(_num(d, "a", 0, imax))
        elif c == "apply":
            p.apply(_num(d, "v", 0, vmax), _num(d, "a", 0, imax))
        elif c == "range":
            p.set_range(str(d.get("r", "")))
        elif c == "ovp":
            lvl = _num(d, "v", OVP_MIN, OVP_MAX) if "v" in d else None
            en = bool(d["on"]) if "on" in d else None
            p.set_ovp(lvl, en)
        elif c == "ocp":
            lvl = _num(d, "a", OCP_MIN, OCP_MAX) if "a" in d else None
            en = bool(d["on"]) if "on" in d else None
            p.set_ocp(lvl, en)
        elif c == "prot_clear":
            p.clear_protection()
        elif c == "save":
            p.save(int(_num(d, "n", 1, 5)))
            changed = False
        elif c == "recall":
            p.recall(int(_num(d, "n", 1, 5)))
        elif c == "disp":
            p.display(bool(d.get("on")))
        elif c == "text":
            p.display_text(str(d.get("s", "")))
            changed = False
        elif c == "text_clear":
            p.display_text_clear()
            changed = False
        elif c == "beep":
            p.beep()
            changed = False
        elif c == "reset":
            p.reset()
        elif c == "local":
            p.local()
            changed = False
        elif c == "remote":
            p.remote(bool(d.get("lock", self.lock_panel)))
            changed = False
        elif c == "trig_src":
            p.trigger_source(str(d.get("s", "")))
        elif c == "trig_del":
            p.trigger_delay(_num(d, "s", 0, 3600))
        elif c == "trig_lvl":
            v = _num(d, "v", 0, vmax) if "v" in d else None
            a = _num(d, "a", 0, imax) if "a" in d else None
            p.trigger_levels(v, a)
        elif c == "init":
            p.initiate()
            changed = False
        elif c == "trg":
            p.trigger()
        elif c == "status":
            self.send_status()
            changed = False
        elif c == "meas":
            self.send_measure()
            changed = False
        elif c == "err":
            res = ["%d,%s" % e for e in p.errors()]
            changed = False
        elif c == "idn":
            res = p.ping()
            changed = False
        elif c == "scpi":
            q = str(d.get("q", "")).strip()
            if not q:
                raise CmdError("SCPI vazio")
            if "?" in q:
                res = p.query(q)
            else:
                p.write(q)
            # comando livre pode ter alterado qualquer coisa
        else:
            raise CmdError("comando desconhecido: %s" % c)

        if changed:
            self._need_status = True
        return res
