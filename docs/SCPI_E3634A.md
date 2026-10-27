# SCPI e RS-232 da E3634A: o que o firmware segue

Referência oficial: **Agilent/Keysight E3634A User's Guide**, part number **E3634-90001**,
capítulo 4 *Remote Interface Reference* (seções *RS-232 Interface Configuration*,
*DTR/DSR Handshake Protocol*, *RS-232 Troubleshooting*, *RS-232 Interface Commands*).

Fontes consultadas:

- [User's Guide E3634-90001 (cópia TME)](https://www.tme.eu/Document/9dd2c7f96457960b51e6c194538b2a4c/E3634-90001.pdf)
- [User's Guide E3634A (cópia RIT)](http://ridl.cfd.rit.edu/products/manuals/Agilent/power%20supplies/CD1/model/E3634AE2.pdf)
- [Keysight: RS232 SYST commands (família E3600)](https://rfmw.em.keysight.com/bihelpfiles/PowerSupport/WHM/Advanced/Content/10%20-%20E3600%20Basic%20power%20supplies/General%20Instrument%20Information/RS232%20SYST%20commands.htm)
- [Keysight E3633A/E3634A User Manual (ManualsLib)](https://www.manualslib.com/manual/1210452/Keysight-E3633a.html)
- [Agilent 34401A: DTR/DSR handshake protocol](https://www.manualsdir.com/manuals/41045/agilent-technologies-34401a.html?page=157) (mesmo texto da família)
- [Agilent AN 5989-6580EN: RS-232 Troubleshooting](https://xdevs.com/doc/HP_Agilent_Keysight/5989-6580EN%20RS-232%20Troubleshooting%20-%20Application%20Note%20c20140910%20%5B8%5D.pdf)
- [Keysight/Agilent E3633A/E3634A User's Guide (cópia widespan.co.kr)](http://www.widespan.co.kr/files/widespan/pdf/E3633A_E3634A_manual.pdf) — um resumo (buscador de IA, não o PDF em si) deste manual dizia 2 stop bits com paridade None, 1 com paridade ativa
- `e3634a_server.py` (implementação de referência do usuário, **validada na fonte real**) — usa 9600 8N**1** sempre, com `dsrdtr`/`rtscts` desligados, pausa mínima de 50 ms entre comandos, e lê `SYST:ERR?` só sob demanda
- [Nota sobre RS-232 em instrumentos Agilent em geral (industrialmonitordirect.com, via 34401A)](https://industrialmonitordirect.com/blogs/knowledgebase/agilent-34401a-rs-232-remote-interface-setup-and-scpi-programming) — "most Agilent instruments must have CTS high (positive) in order to communicate"

> Os PDFs não abriram neste ambiente (o proxy bloqueia os sites). Os itens marcados **✔** foram
> confirmados nos trechos dos próprios manuais devolvidos pela busca. Os demais vêm do
> conteúdo conhecido do manual da família E363x e batem com o Lab Test, que funcionou na fonte
> real. Confira-os na seção indicada do guia se algo divergir.
>
> **Stop bits:** um resumo de busca (não o PDF em si) chegou a indicar "2 bits com paridade
> None, 1 com paridade ativa". A referência real que **funciona na fonte** (`e3634a_server.py`
> do usuário) usa **sempre 1**, com 8N. O firmware segue essa segunda fonte, por ser
> implementação validada e não um resumo de terceiros.

## 1. Configuração da porta

| Item | Valor | Firmware |
|---|---|---|
| Interface | `RS-232` no menu I/O Config (padrão de fábrica é GPIB/HPIB) | — |
| Velocidade | 300, 600, 1200, 2400, 4800, **9600** (fábrica) | `PSU_BAUD 9600` |
| Paridade/bits | **None 8 bits** (fábrica), Even 7 bits, Odd 7 bits | 8N |
| Start/stop | 1 start bit; **1 stop bit** | `PSU_STOP_BITS 1` |
| Conector | DB9 macho, a fonte é **DTE** ✔: 2 = RXD (entra), 3 = TXD (sai), 4 = DTR (sai), 5 = GND, **6 = DSR (entra)**, 7 = RTS (sai), **8 = CTS (entra)** | cabo cruzado 2↔3, 4↔6, 7↔8 |

## 2. Handshake DTR/DSR **e** RTS/CTS (a causa do `timeout rx=vazio`)

Um cabo null-modem de fábrica cruza **as duas** linhas de controle (4↔6 e 7↔8), e tanto o DTR
quanto o RTS de um PC ficam em nível alto por padrão quando a porta abre — por isso um PC com
cabo de fábrica (como o Lab Test) nunca precisa se preocupar com isso. Um módulo MAX3232 de 4
pinos não tem nenhuma das duas linhas, então **as duas precisam ser forçadas por resistor** —
só o DSR não é o bastante se a fonte também checar o CTS (ver item "RTS/CTS" abaixo).

- A fonte é DTE e usa **DTR (pino 4)** e **DSR (pino 6)** para o handshake ✔.
- **O DTR precisa estar TRUE para a fonte aceitar dados** ✔; ela usa o DTR como sinal de
  espera (*hold-off*) ✔.
- Ela põe o **DTR em FALSO** quando o buffer de entrada enche (≈100 caracteres) ✔ e
  **quando quer falar**: depois de processar uma query e receber o `<new line>` ✔ (texto do
  34401A, mesma família). Volta a TRUE depois de mandar a resposta.
- **Confere o DSR antes de cada caractere; com DSR FALSO a saída fica suspensa** ✔ e o DTR
  continua FALSO enquanto isso ✔.
- O travamento se desfaz com **`<Ctrl-C>`**, que descarta a saída pendente.
- Sem handshake: **DTR desligado e DSR em nível TRUE**.

**RTS/CTS:** segundo uma nota de aplicação da Agilent sobre RS-232 (não específica da E3634A,
tratar como reforço, não confirmação direta do manual desta fonte), a maioria dos instrumentos
Agilent **também** exige o **CTS (pino 8) em nível TRUE** para se comunicar — independente do
DTR/DSR. Sem handshake: **RTS desligado e CTS em nível TRUE** (mesma lógica do DSR, pino 7↔8 em
vez de 4↔6).

Por isso:

| Ligação | Resultado | Simulado em |
|---|---|---|
| Pinos 6 e 8 em +5 V por 1 kΩ cada, pinos 4 e 7 soltos | Funciona | `tests/test_core.c` `t_app_session`, `tests/e2e_serial.py` |
| Só o pino 6 em +5 V, pino 8 (CTS) solto | Pode continuar mudo, se a fonte também checar o CTS | — (hipótese do AN da Agilent, não simulada) |
| Jumper 4↔6 | Aceita comandos (`RMT` acende), **trava na 1ª query**; comandos seguintes dão `-410` e beep | `t_jumper_4_6_deadlock` |
| Pino 6 solto | Aceita comandos, **nunca responde** | `t_dsr_open` |
| PC com adaptador USB + cabo null-modem de fábrica (Lab Test) | Funciona: o DTR e o RTS do PC deixam o DSR e o CTS da fonte TRUE | `host/pc_bridge` |

## 2.1 Pausa mínima entre comandos

Segundo a implementação de referência do usuário (`e3634a_server.py`, validada na fonte real):
a E3634A **perde caracteres** se dois comandos chegarem muito colados. Ela usa uma pausa mínima
de **50 ms** antes de cada envio, independente de ter lido uma resposta no meio. O firmware
aplica isso em `psu.c`: todo envio espera `PSU_MIN_GAP_MS` (50 ms, `config.h`) desde o fim do
envio anterior.

## 3. Comandos exclusivos da RS-232

| Comando | Efeito |
|---|---|
| `SYSTem:REMote` | Modo remoto: teclas do painel desligadas, menos *Local*. **Obrigatório antes de programar pela RS-232** ✔ (manual da E3631A; vale para a família) |
| `SYSTem:RWLock` | Igual, mas trava também a tecla *Local* |
| `SYSTem:LOCal` | Volta ao painel |
| `<Ctrl-C>` (0x03) | Cancela a operação em curso e descarta a saída pendente (= *device clear* do GPIB) |

## 4. Formato das mensagens

- O comando termina em `<new line>` (0x0A); o firmware manda **`\r\n`**, como o Lab Test.
- A resposta termina em `\r\n`; o firmware aceita `\r`, `\n` ou os dois.
- Vários comandos numa linha: separados por `;` e `:` para voltar à raiz (`VOLT 5;:CURR 1`).
- Não mande outro comando antes de ler a resposta de uma query: a fonte descarta a resposta e
  registra **`-410 Query INTERRUPTED`** (e apita). O firmware **nunca** limpa a entrada ao
  escrever, só antes de uma query.
- Números chegam como `+1.20000000E+01`; booleanos como `0`/`1`.

## 5. Sequência de abertura (igual ao Lab Test)

```
<Ctrl-C>                 descarta resposta presa de uma sessão anterior
(150 ms)
*CLS                     limpa status e fila de erros
SYST:ERR?  (até +0)      esvazia a fila: sem beep
*IDN?                    HEWLETT-PACKARD,E3634A,0,X.X-X.X-X.X
SYST:REM                 modo remoto (ou SYST:RWL)
```

Depois de cada escrita o firmware lê `SYST:ERR?` para validar o comando (o app recebe o erro
SCPI, por exemplo `-222 Data out of range`).

## 6. Comandos usados pelo firmware

| App (`cmd`) | SCPI | Resposta |
|---|---|---|
| medição | `MEAS:VOLT:DC?` · 50 ms · `MEAS:CURR:DC?` | `+5.00000000E+00` |
| | `OUTP?`, `VOLT:PROT:TRIP?`, `CURR:PROT:TRIP?` (a cada 5 leituras) | `0`/`1` |
| `out` | `OUTP ON` / `OUTP OFF` | — |
| `volt` / `curr` / `apply` | `VOLT 5.0000` / `CURR 1.0000` / `APPL 5.0000,1.0000` | — |
| `range` | `VOLT:RANG P25V` / `P50V` | — |
| `ovp` / `ocp` | `VOLT:PROT 30.000`, `VOLT:PROT:STAT ON`, `CURR:PROT 5.0000`, `CURR:PROT:STAT ON` | — |
| `prot_clear` | `VOLT:PROT:CLE`, `CURR:PROT:CLE` | — |
| `save` / `recall` | `*SAV n` / `*RCL n` (n = 1..5) | — |
| `disp` / `text` / `text_clear` | `DISP ON`, `DISP:TEXT "ate 12 car"`, `DISP:TEXT:CLE` | — |
| `beep` / `reset` | `SYST:BEEP` / `*RST` | — |
| `local` / `remote` | `SYST:LOC` / `SYST:REM` ou `SYST:RWL` | — |
| `trig_src` / `trig_del` / `trig_lvl` | `TRIG:SOUR BUS`/`IMM`, `TRIG:DEL s`, `VOLT:TRIG v`, `CURR:TRIG a` | — |
| `init` / `trg` | `INIT` / `*TRG` | — |
| `status` | `VOLT:RANG?`, `VOLT?`, `CURR?`, `OUTP?`, `VOLT:PROT?`, `VOLT:PROT:STAT?`, `CURR:PROT?`, `CURR:PROT:STAT?`, `DISP?`, `TRIG:SOUR?`, `TRIG:DEL?`, `VOLT:TRIG?`, `CURR:TRIG?` | |
| `err` | `SYST:ERR?` até `+0,"No error"` | `-222,"Data out of range"` |
| `idn` | `*IDN?` | |
| `scpi` | texto livre | |

Limites programáveis (validados no ESP e de novo pela fonte):

| Faixa | V máx | A máx |
|---|---|---|
| P25V | 25,75 V | 7,21 A |
| P50V | 51,5 V | 4,12 A |
| OVP | 1 a 55 V | |
| OCP | 0 a 7,5 A | |
