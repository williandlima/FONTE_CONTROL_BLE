# Protocolo BLE (app ↔ ESP32)

Transporte: **Nordic UART Service (NUS)**

| Característica | UUID | Direção |
|---|---|---|
| Serviço | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | |
| RX | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | app → ESP (write) |
| TX | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | ESP → app (notify) |

Cada mensagem é **um objeto JSON terminado em `\n`**. Mensagens maiores que o
MTU são fatiadas e remontadas pelo `\n`. Qualquer app BLE-UART genérico
(nRF Connect, Serial Bluetooth Terminal) serve para testar.

## App → ESP (comandos)

Todos aceitam `"id"` opcional, devolvido na resposta.

| `cmd` | Campos | SCPI gerado |
|---|---|---|
| `out` | `on`: bool | `OUTP ON/OFF` |
| `volt` | `v`: V | `VOLT v` |
| `curr` | `a`: A | `CURR a` |
| `apply` | `v`, `a` | `APPL v,a` |
| `range` | `r`: `P25V`/`P50V` (ou `LOW`/`HIGH`) | `VOLT:RANG` |
| `ovp` | `v`? (1–55 V), `on`? | `VOLT:PROT`, `VOLT:PROT:STAT` |
| `ocp` | `a`? (0–7,5 A), `on`? | `CURR:PROT`, `CURR:PROT:STAT` |
| `prot_clear` | — | `VOLT:PROT:CLE` + `CURR:PROT:CLE` |
| `save` / `recall` | `n`: 1–5 | `*SAV n` / `*RCL n` |
| `disp` | `on` | `DISP ON/OFF` |
| `text` | `s` (≤12 caracteres) | `DISP:TEXT "s"` |
| `text_clear` | — | `DISP:TEXT:CLE` |
| `beep` | — | `SYST:BEEP` |
| `reset` | — | `*RST` |
| `local` | — | `SYST:LOC` |
| `remote` | `lock`? bool | `SYST:REM` / `SYST:RWL` |
| `trig_src` | `s`: `BUS`/`IMM` | `TRIG:SOUR` |
| `trig_del` | `s`: 0–3600 | `TRIG:DEL` |
| `trig_lvl` | `v`?, `a`? | `VOLT:TRIG`, `CURR:TRIG` |
| `init` / `trg` | — | `INIT` / `*TRG` |
| `scpi` | `q`: texto SCPI livre | query se contém `?` |
| `status` | — | relê toda a configuração → `st` |
| `meas` | — | medição imediata → `m` |
| `err` | — | esvazia `SYST:ERR?` |
| `idn` | — | `*IDN?` |
| `poll` | `ms`: 0 (desliga) ou ≥150 | intervalo da medição automática |
| `link` | — | força nova tentativa de conexão com a fonte |
| `ping` | — | — |
| `version` | — | devolve a versão do firmware (`d`); funciona sem a fonte conectada |

Exemplos:

```json
{"cmd":"apply","v":12.0,"a":0.5,"id":1}
{"cmd":"out","on":true,"id":2}
{"cmd":"scpi","q":"MEAS:VOLT?","id":3}
```

Limites de V/A são validados no ESP pela faixa atual (P25V: 25,75 V/7,21 A;
P50V: 51,5 V/4,12 A) **e** novamente pela fonte (`SYST:ERR?` após cada escrita).

## ESP → App (eventos)

| `t` | Quando | Campos |
|---|---|---|
| `hello` | App conectou | `fw`, `link`, `idn`?, `poll` |
| `link` | Mudou o estado do RS-232 | `ok`, `idn`?, `err`? |
| `m` | A cada `poll` ms | `v`, `i`, `o` (saída), `ovp_t`, `ocp_t` |
| `st` | Após conectar / alterar algo / `status` | `range`, `vset`, `iset`, `out`, `ovp`, `ovp_en`, `ocp`, `ocp_en`, `disp`, `trig_src`, `trig_del`, `vtrig`, `itrig`, `poll` |
| `r` | Resposta a cada comando | `ok`, `cmd`, `d`? (dado), `err`?, `code`? (erro SCPI), `id`? |
| `e` | Erro SCPI fora de comando | `code`, `msg` |

```json
{"t":"m","v":12.0012,"i":0.49871,"o":true,"ovp_t":false,"ocp_t":false}
{"t":"r","ok":false,"err":"SCPI -222: Data out of range","code":-222,"id":7}
```

## Leitura de medição

Opcionalmente o firmware usa uma **query composta** (uma única transação RS-232):

```
MEAS:VOLT:DC?;:MEAS:CURR:DC?;:OUTP?;:VOLT:PROT:TRIP?;:CURR:PROT:TRIP?
```

Padrão: `PSU_COMPOUND_QUERY 0` em `firmware/core/config.h` (`MEAS:VOLT:DC?`, 50 ms,
`MEAS:CURR:DC?`; `OUTP?` e os `TRIP?` a cada 5 leituras), o método validado no Lab Test.
Com `1` usa a query composta acima (uma transação, mais rápida).
