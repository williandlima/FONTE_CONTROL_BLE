import json
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "firmware"))
sys.path.insert(0, HERE)

from e3634a import E3634A, LinkError, SCPIError  # noqa: E402
from bridge import Bridge  # noqa: E402
from sim_e3634a import SimE3634A  # noqa: E402


class DriverTest(unittest.TestCase):
    def setUp(self):
        self.sim = SimE3634A(load_ohm=10)
        self.psu = E3634A(self.sim, timeout_ms=200)
        self.psu.connect()

    def test_connect_sets_remote(self):
        self.assertTrue(self.sim.remote)
        self.assertIn("E3634A", self.psu.idn)

    def test_cv_measure(self):
        self.psu.apply(5.0, 1.0)
        self.psu.output(True)
        v, i, out, ovp, ocp = self.psu.measure()
        self.assertAlmostEqual(v, 5.0)
        self.assertAlmostEqual(i, 0.5)
        self.assertTrue(out)
        self.assertFalse(ovp or ocp)

    def test_cc_measure_non_compound(self):
        self.psu.compound = False
        self.psu.apply(20.0, 1.0)
        self.psu.output(True)
        v, i, *_ = self.psu.measure()
        self.assertAlmostEqual(i, 1.0)
        self.assertAlmostEqual(v, 10.0)

    def test_scpi_error_raised(self):
        with self.assertRaises(SCPIError) as cm:
            self.psu.set_voltage(40.0)  # acima de 25.75 V na faixa P25V
        self.assertEqual(cm.exception.code, -222)

    def test_range_and_status(self):
        self.psu.set_range("HIGH")
        self.psu.set_ovp(30.0, True)
        st = self.psu.status()
        self.assertEqual(st["range"], "P50V")
        self.assertEqual(st["ovp"], 30.0)
        self.assertTrue(st["ovp_en"])

    def test_ovp_trip_and_clear(self):
        self.psu.set_ovp(4.0, True)
        self.psu.apply(5.0, 1.0)
        self.psu.output(True)
        _, _, out, ovp_t, _ = self.psu.measure()
        self.assertTrue(ovp_t)
        self.assertFalse(out)
        self.psu.clear_protection()
        self.assertFalse(self.psu.measure()[3])

    def test_timeout(self):
        self.sim.alive = False
        with self.assertRaises(LinkError):
            self.psu.query("*IDN?")

    def test_display_text_truncated(self):
        self.psu.display_text('ola "mundo" 123456')
        self.assertEqual(len(self.sim.text), 12)
        self.assertNotIn('"', self.sim.text)


class BridgeTest(unittest.TestCase):
    def setUp(self):
        self.sim = SimE3634A(load_ohm=10)
        self.psu = E3634A(self.sim, timeout_ms=200)
        self.out = []
        self.br = Bridge(self.psu, self.out.append, poll_ms=0, reconnect_ms=0)
        self.br.tick()                 # liga a fonte
        self.br.on_client(True)
        self.br.tick()                 # envia status
        self.out.clear()

    def msgs(self, t=None):
        m = [json.loads(x) for x in self.out]
        return [x for x in m if t is None or x["t"] == t]

    def cmd(self, **kw):
        self.out.clear()
        self.br.handle_line(json.dumps(kw))
        r = self.msgs("r")
        self.assertEqual(len(r), 1)
        return r[0]

    def test_link_up(self):
        self.assertTrue(self.br.link)

    def test_apply_and_status_refresh(self):
        r = self.cmd(cmd="apply", v=12, a=0.5, id=7)
        self.assertTrue(r["ok"])
        self.assertEqual(r["id"], 7)
        self.out.clear()
        self.br.tick()
        st = self.msgs("st")[0]
        self.assertEqual(st["vset"], 12.0)
        self.assertEqual(st["iset"], 0.5)

    def test_limits_checked_locally(self):
        r = self.cmd(cmd="volt", v=30)       # faixa P25V
        self.assertFalse(r["ok"])
        self.assertNotIn("VOLT 30", " ".join(self.sim.log))

    def test_scpi_passthrough(self):
        r = self.cmd(cmd="scpi", q="*IDN?")
        self.assertIn("E3634A", r["d"])
        r = self.cmd(cmd="scpi", q="FOO:BAR")
        self.assertFalse(r["ok"])
        self.assertEqual(r["code"], -113)

    def test_poll_measure(self):
        self.cmd(cmd="apply", v=5, a=1)
        self.cmd(cmd="out", on=True)
        self.cmd(cmd="poll", ms=150)
        self.br._t_poll = -10000
        self.out.clear()
        self.br.tick()
        m = self.msgs("m")
        self.assertEqual(len(m), 1)
        self.assertAlmostEqual(m[0]["v"], 5.0)
        self.assertAlmostEqual(m[0]["i"], 0.5)
        self.assertTrue(m[0]["o"])

    def test_poll_min(self):
        r = self.cmd(cmd="poll", ms=10)
        self.assertEqual(r["d"], 150)

    def test_link_lost_and_recovered(self):
        self.sim.alive = False
        r = self.cmd(cmd="out", on=True)
        self.assertFalse(r["ok"])
        self.assertFalse(self.br.link)
        self.assertTrue(any(m["t"] == "link" and not m["ok"] for m in self.msgs()))
        r = self.cmd(cmd="volt", v=1)
        self.assertEqual(r["err"], "fonte desconectada")
        self.sim.alive = True
        self.sim.remote = False          # fonte religada volta em LOCAL
        self.out.clear()
        self.br.tick()
        self.assertTrue(self.br.link)
        self.assertTrue(self.sim.remote)

    def test_retry_backoff(self):
        self.br.reconnect_ms = 3000
        self.br._fails = 0
        self.assertEqual(self.br._retry_ms(), 3000)
        self.br._fails = 2
        self.assertEqual(self.br._retry_ms(), 12000)
        self.br._fails = 10
        self.assertEqual(self.br._retry_ms(), 30000)

    def test_bad_json(self):
        self.out.clear()
        self.br.handle_line("{nao json")
        self.assertFalse(self.msgs("r")[0]["ok"])

    def test_memory(self):
        self.cmd(cmd="apply", v=3.3, a=0.2)
        self.assertTrue(self.cmd(cmd="save", n=2)["ok"])
        self.cmd(cmd="apply", v=9, a=1)
        self.assertTrue(self.cmd(cmd="recall", n=2)["ok"])
        self.assertEqual(self.sim.vset, 3.3)
        self.assertFalse(self.cmd(cmd="save", n=9)["ok"])

    def test_all_commands_accept(self):
        for c in [dict(cmd="range", r="P50V"), dict(cmd="ovp", v=20, on=True),
                  dict(cmd="ocp", a=2, on=False), dict(cmd="prot_clear"),
                  dict(cmd="disp", on=False), dict(cmd="text", s="TESTE"),
                  dict(cmd="text_clear"), dict(cmd="beep"),
                  dict(cmd="trig_src", s="BUS"), dict(cmd="trig_del", s=2),
                  dict(cmd="trig_lvl", v=4, a=1), dict(cmd="init"),
                  dict(cmd="trg"), dict(cmd="status"), dict(cmd="meas"),
                  dict(cmd="err"), dict(cmd="idn"), dict(cmd="remote"),
                  dict(cmd="reset"), dict(cmd="ping")]:
            r = self.cmd(**c)
            self.assertTrue(r["ok"], (c, r))
        self.assertTrue(self.cmd(cmd="local")["ok"])


if __name__ == "__main__":
    unittest.main()
