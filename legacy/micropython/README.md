# Firmware MicroPython (1.1.5) - substituído

Versão anterior da ponte, em MicroPython, gravada pelo Thonny. Foi substituída pelo
firmware em C de [`firmware/`](../../firmware) (ver [docs/FIRMWARE_C.md](../../docs/FIRMWARE_C.md)),
que usa o mesmo protocolo BLE: o app Android funciona com os dois.

Fica aqui só como referência. Os testes ainda rodam:

```bash
cd legacy/micropython && python3 -m unittest discover -s tests
```
