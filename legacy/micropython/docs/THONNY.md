# Gravar e conferir o firmware com o Thonny

## 1. Conectar
1. **Tools → Options → Interpreter** → *MicroPython on a generic device* → escolha a porta
   ("USB JTAG/serial debug unit @ COMx") → **OK**.
2. **View → Shell** e **View → Files**.
3. Clique no **Stop** (Ctrl+F2) e espere o `>>>`.

## 2. Enviar
No painel de cima ("This computer"), vá até a pasta `firmware`, selecione os 7 arquivos
(`ble_uart.py`, `boot.py`, `bridge.py`, `config.py`, `e3634a.py`, `main.py`, `selftest.py`),
botão direito → **Upload to /** → **OK** se perguntar sobre sobrescrever.

## 3. Conferir que gravou (nesta ordem)

**a) Arquivos e tamanhos.** No Shell (com `>>>`):

```python
import os; _ = [print(f, os.stat(f)[6]) for f in sorted(os.listdir())]
```

Nenhum pode ter tamanho `0`. Devem existir os 7 arquivos.

**b) Versão do firmware.** No Shell:

```python
import bridge; print(bridge.FW_VERSION)
```

Tem de imprimir a versão atual (a mesma de `python tools/fw_manifest.py`, no fim da lista).

**c) Conteúdo idêntico ao do PC (hash).** No Shell, numa linha só:

```python
import os,hashlib,binascii; _ = [print(f, os.stat(f)[6], binascii.hexlify(hashlib.sha256(open(f,'rb').read()).digest())[:8].decode()) for f in sorted(os.listdir()) if f.endswith('.py')]
```

No PC, na pasta do projeto:

```
python tools/fw_manifest.py
```

As linhas (nome, tamanho e hash) têm de ser **iguais** nos dois. Se um arquivo difere, envie
só ele de novo.

**d) Ao iniciar.** Aperte **Ctrl+D** no Shell. Na versão atual aparece:

```
Janela de seguranca (3 s): Ctrl+C ou segure BOOT para ficar no REPL
E3634A-BLE pronto. BLE: E3634A-BLE
```

Se faltar a primeira linha, o `main.py` da placa é antigo.

**e) No app.** Conectado ao `E3634A-BLE`, o cabeçalho mostra `FW x.y.z` e a aba **Mais →
Sistema** mostra `Firmware da ponte`. Tem de ser a mesma versão do item (b).

## Regras
- O Shell só aceita comandos com o `>>>` na tela; se o `main.py` estiver rodando, clique em **Stop**.
- Nunca edite o `main.py` do painel da placa e não use o botão verde *Run* com esses arquivos.
- Feche o Thonny antes de usar `mpremote` ou o VS Code na mesma porta.
