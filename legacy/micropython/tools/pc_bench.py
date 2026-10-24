#!/usr/bin/env python3
"""Teste de bancada da E3634A direto do PC (cabo USB-RS232), sem o ESP32.

Usa o mesmo driver do firmware (firmware/e3634a.py) para validar baud,
handshake, cabo e comandos antes de montar a ponte BLE.

    pip install pyserial
    python tools/pc_bench.py --port COM5            # Windows
    python tools/pc_bench.py --port /dev/ttyUSB0    # Linux
    python tools/pc_bench.py --sim                  # simulador, sem hardware

Comandos no prompt: qualquer SCPI (ex: VOLT 5, MEAS:VOLT?), ou
  :mon [ms]   monitora V/I ate Ctrl+C
  :st         mostra a configuracao completa
  :err        le a fila de erros
  :q          sai
"""
import argparse
import os
import sys
import time

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "firmware"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

from e3634a import E3634A, LinkError, SCPIError  # noqa: E402


class SerialUART:
    """Adapta pyserial para a interface write/read/any do driver."""

    def __init__(self, port, baud, stop, dsrdtr):
        import serial
        self.s = serial.Serial(
            port, baudrate=baud, bytesize=8, parity="N",
            stopbits=serial.STOPBITS_TWO if stop == 2 else serial.STOPBITS_ONE,
            timeout=0, dsrdtr=dsrdtr)
        self.s.dtr = True   # PC pronto (vai no DSR da fonte com cabo null-modem)

    def write(self, b):
        return self.s.write(b)

    def any(self):
        return self.s.in_waiting

    def read(self, n=-1):
        return self.s.read(n if n and n > 0 else self.s.in_waiting)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--stop", type=int, default=1, choices=(1, 2))
    ap.add_argument("--dsrdtr", action="store_true", help="handshake DTR/DSR no PC")
    ap.add_argument("--sim", action="store_true")
    ap.add_argument("-v", action="store_true", help="mostra o trafego SCPI")
    a = ap.parse_args()

    if a.sim:
        from sim_e3634a import SimE3634A
        uart = SimE3634A(load_ohm=10)
    elif a.port:
        uart = SerialUART(a.port, a.baud, a.stop, a.dsrdtr)
    else:
        ap.error("informe --port ou --sim")

    psu = E3634A(uart, log=print if a.v else None)
    try:
        print("Conectado:", psu.connect())
    except LinkError as e:
        print("Sem resposta da fonte (%s). Verifique baud, TX/RX cruzado, "
              "GND e o DSR (pino 6) verdadeiro." % e)
        return 1

    while True:
        try:
            line = input("scpi> ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        if not line:
            continue
        try:
            if line == ":q":
                break
            elif line.startswith(":mon"):
                ms = int(line.split()[1]) if len(line.split()) > 1 else 500
                try:
                    while True:
                        v, i, o, ovp, ocp = psu.measure()
                        print("\r%9.4f V  %8.5f A  %8.3f W  OUT=%s%s%s   " % (
                            v, i, v * i, "ON " if o else "OFF",
                            " OVP!" if ovp else "", " OCP!" if ocp else ""),
                            end="", flush=True)
                        time.sleep(ms / 1000)
                except KeyboardInterrupt:
                    print()
            elif line == ":st":
                for k, v in psu.status().items():
                    print("  %-9s %s" % (k, v))
            elif line == ":err":
                print(psu.errors() or "sem erros")
            elif "?" in line:
                print(psu.query(line))
            else:
                psu.write(line)
                print("ok")
        except SCPIError as e:
            print("ERRO SCPI:", e)
        except LinkError as e:
            print("ERRO LINK:", e)
    psu.local()
    return 0


if __name__ == "__main__":
    sys.exit(main())
