#!/usr/bin/env python3
"""Lista nome, tamanho e hash (8 hex) dos arquivos do firmware.

O formato e o mesmo do comando que roda na placa (ver docs/THONNY.md), para
conferir se o que esta gravado no ESP32 e igual ao que esta neste PC:

    python tools/fw_manifest.py
"""
import hashlib
import os
import re

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware")

files = sorted(f for f in os.listdir(ROOT) if f.endswith(".py"))
for f in files:
    data = open(os.path.join(ROOT, f), "rb").read()
    print(f, len(data), hashlib.sha256(data).hexdigest()[:8])

m = re.search(r'FW_VERSION\s*=\s*"([^"]+)"', open(os.path.join(ROOT, "bridge.py")).read())
print("\nVersao esperada do firmware:", m.group(1) if m else "?")
