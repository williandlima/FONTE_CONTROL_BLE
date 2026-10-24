#!/usr/bin/env python3
"""Teste de ponta a ponta simulado, com processos e porta serial de verdade.

    app (este script, JSON igual ao app Android)
      -> pc_bridge (nucleo C do firmware: bridge.c + psu.c + json.c)
      -> porta serial virtual (pty, termios 9600 8N2)
      -> sim_serial (E3634A simulada, com o handshake DTR/DSR)

Roda tres ligacoes do cabo: correta (pino 6 em +5 V), jumper 4-6 e pino 6 solto.

    make -C tests e2e
"""
import os
import queue
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.path.join(HERE, "..", "host")
FAILS = []


def check(cond, what):
    print(("  ok      " if cond else "  FALHOU  ") + what)
    if not cond:
        FAILS.append(what)


class Bench:
    def __init__(self, wiring):
        self.sim = subprocess.Popen([os.path.join(HOST, "sim_serial"), "--wiring", wiring],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=open(os.path.join(HERE, "e2e_sim_%s.log" % wiring), "w"),
                                    text=True)
        self.port = self.sim.stdout.readline().strip()
        self.fw = subprocess.Popen([os.path.join(HOST, "pc_bridge"), self.port],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=open(os.path.join(HERE, "e2e_fw_%s.log" % wiring), "w"),
                                   text=True, bufsize=1)
        self.q = queue.Queue()
        self.seen = []
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        import json
        for line in self.fw.stdout:
            try:
                self.q.put(json.loads(line))
            except ValueError:
                self.q.put({"t": "?", "raw": line})

    def send(self, line):
        self.fw.stdin.write(line + "\n")
        self.fw.stdin.flush()

    def wait(self, pred, timeout=8.0):
        end = time.time() + timeout
        while time.time() < end:
            try:
                m = self.q.get(timeout=0.05)
            except queue.Empty:
                continue
            self.seen.append(m)
            if pred(m):
                return m
        return None

    def reply(self, rid, timeout=8.0):
        return self.wait(lambda m: m.get("t") == "r" and m.get("id") == rid, timeout)

    def close(self):
        self.fw.stdin.close()
        self.fw.wait(5)
        self.sim.stdin.close()
        self.sim.wait(5)


def wired_ok():
    print("ligacao correta: pino 6 em +5 V por 1 kOhm, pino 4 solto")
    b = Bench("ok")
    h = b.wait(lambda m: m.get("t") == "hello")
    check(h is not None and h.get("fw"), "hello com versao do firmware (%s)" % (h or {}).get("fw"))
    lk = b.wait(lambda m: m.get("t") == "link")
    check(lk is not None and lk["ok"] and "E3634A" in lk.get("idn", ""), "link ok com *IDN?")
    st = b.wait(lambda m: m.get("t") == "st")
    check(st is not None and st["range"] == "P25V", "status inicial (faixa P25V)")

    b.send('{"cmd":"apply","v":5.0,"a":1.0,"id":1}')
    check((b.reply(1) or {}).get("ok"), "apply 5 V / 1 A")
    b.send('{"cmd":"out","on":true,"id":2}')
    check((b.reply(2) or {}).get("ok"), "saida ligada")
    m = b.wait(lambda m: m.get("t") == "m" and m.get("o"))
    check(m is not None and abs(m["v"] - 5.0) < 1e-6 and abs(m["i"] - 0.5) < 1e-6,
          "medicao periodica 5 V / 0,5 A (carga 10 ohm): %r" % ((m and (m["v"], m["i"])),))

    b.send('{"cmd":"volt","v":40,"id":3}')
    r = b.reply(3) or {}
    check(not r.get("ok") and "fora da faixa" in r.get("err", ""), "40 V barrado no firmware")
    b.send('{"cmd":"scpi","q":"VOLT 40","id":4}')
    r = b.reply(4) or {}
    check(r.get("code") == -222, "40 V recusado pela fonte: %s" % r.get("err"))
    b.send('{"cmd":"scpi","q":"MEAS:CURR?","id":5}')
    r = b.reply(5) or {}
    check(r.get("ok") and float(r.get("d", "nan")) == 0.5, "SCPI livre MEAS:CURR? = %s" % r.get("d"))
    b.send('{"cmd":"version","id":6}')
    check((b.reply(6) or {}).get("d") == h.get("fw"), "versao")
    n = sum(1 for x in b.seen if x.get("t") == "m")
    b.wait(lambda m: False, 1.5)
    n2 = sum(1 for x in b.seen if x.get("t") == "m")
    check(n2 - n >= 2, "medicoes continuam (%d em 1,5 s)" % (n2 - n))
    b.close()


def wired_bad(wiring, label):
    print(label)
    b = Bench(wiring)
    h = b.wait(lambda m: m.get("t") == "hello")
    check(h is not None and h["link"] is False, "hello com link falso")
    ok = b.wait(lambda m: m.get("t") == "link" and m.get("ok"), 4.0)
    check(ok is None, "nunca conecta")
    b.send('{"cmd":"link","id":9}')
    r = b.reply(9, 12) or {}
    check(not r.get("ok") and "timeout rx=vazio" in r.get("err", ""),
          "comando link explica: %s" % r.get("err"))
    b.send('{"cmd":"out","on":true,"id":10}')
    check((b.reply(10) or {}).get("err") == "fonte desconectada", "comandos recusados sem a fonte")
    b.close()


def main():
    subprocess.check_call(["make", "-s", "-C", HOST, "pc_bridge", "sim_serial"])
    wired_ok()
    wired_bad("jumper", "jumper 4-6 (DSR segue o DTR da fonte): trava ao responder")
    wired_bad("open", "pino 6 solto (DSR falso): a fonte nunca transmite")
    print("\n%s" % ("TUDO OK" if not FAILS else "%d FALHAS" % len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
