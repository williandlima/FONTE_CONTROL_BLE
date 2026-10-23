# Hardware e ligações

## Lista de material

| Item | Observação |
|---|---|
| ESP32-C3 Super Mini | BLE 5, USB-C nativo (USB-CDC), 3,3 V |
| Módulo conversor RS-232 ↔ TTL **MAX3232** | Precisa funcionar em **3,3 V** (MAX232 "puro" é só 5 V — não use) |
| Conector DB9 **fêmea** (solda) ou cabo | Para encaixar no DB9 macho traseiro da E3634A |
| Fonte 5 V USB-C (carregador de celular) | Alimenta o ESP32 e o MAX3232 |

> Modelo assumido: **Agilent/Keysight E3634A** (25 V/7 A e 50 V/4 A, RS-232 + GPIB).
> As irmãs E3632A/E3633A usam o mesmo conjunto SCPI (mudam só os limites em
> `firmware/e3634a.py` → `RANGES`).

## Seu hardware (conferido pela foto)

- **ESP32-C3 Super Mini**: GPIO21 e GPIO20 são os dois primeiros pinos da fileira
  superior (lado oposto ao 5V); `3V3`, `G` e `5V` ficam juntos junto ao USB-C.
- **Módulo RS-232 azul** (U1 de 16 pinos, capacitores C1–C5, LED D1, DB9 "9P"):
  header de 4 pinos **GND, TX, RX, VCC** — só o canal 1 (T1/R1). Não existe
  DTR/DSR no módulo, então `DTR_IN_PIN`/`DSR_OUT_PIN` ficam `None`, o
  **pino 6 (DSR) recebe +5 V por 1 kΩ** e o **pino 4 (DTR) fica solto**.

| Módulo | Super Mini |
|---|---|
| VCC | **3V3** (nunca 5V) |
| GND | G |
| TX | GPIO21 |
| RX | GPIO20 |
| pino 6 do DB9 (DSR) | **5V por resistor de 1 kΩ** |

A rotulagem TX/RX desses módulos varia. Não adivinhe: rode o autoteste
(`selftest.loopback()`, abaixo) e ele diz se precisa inverter os dois fios.

### Três coisas a conferir antes de ligar na fonte

1. **Marcação do chip U1.** Tem que ser **MAX3232** (ou SP3232/ST3232/
   ADM3232). Se for **MAX232** puro, ele só funciona em 5V e a saída R1OUT
   chegaria a 5V no GPIO20 do ESP32-C3 — não ligue.
2. **Gênero e tipo do DB9.** Esses módulos costumam ser fiados como **DTE**
   (igual a um PC: TXD no pino 3, RXD no pino 2). A E3634A também é DTE, então
   entre as duas pontas o cabo precisa ser **null-modem (cruzado)**: 2↔3 e 5↔5.
   Um cabo reto/extensor não funciona. Se os gêneros dos conectores não
   encaixam, use um adaptador de gênero ou faça um cabo curto.
3. **Pino 6 em +5 V por 1 kΩ, pino 4 solto, sem jumper 4↔6** (ver abaixo).

### Diagnóstico automático da serial

Com a fonte ligada e o `main.py` parado (**Stop** no Thonny), no Shell:

```python
import selftest
selftest.diag()     # nível do RX, escuta passiva e *IDN? em várias configurações
selftest.beep()     # só o caminho ESP -> fonte: a fonte deve mostrar RMT e apitar uma vez
```

- **`diag()` acha a fonte:** imprime o `config.py` a usar.
- **Devolve bytes estranhos:** velocidade, paridade ou formato diferentes (I/O Config da fonte).
- **Mudo em tudo + `beep()` faz a fonte reagir:** os comandos chegam, falta o caminho de
  volta (pino 3 da fonte, GND, fio do RX do ESP).
- **Mudo em tudo + `beep()` não faz nada:** falta o caminho de ida (cabo cruzado 2↔3, fio do TX).

Obs.: o ESP32‑C3 imprime o log de boot na UART0 (GPIO21), a 115200. Apitos da fonte só
**na hora de ligar ou resetar** o ESP vêm disso; se incomodar, mude `UART_TX`/`UART_RX`
no `config.py` para outros GPIOs (por exemplo 4 e 3) e religue os fios.

`selftest.edges()` não decodifica nada: conta transições no pino RX depois de `*IDN?`.
Resultado maior que zero significa que a fonte transmite (então é baud/formato); zero
significa que nada chega ao pino (DSR, fio do pino 3, módulo, fonte travada).

### Roteiro de teste (nesta ordem)

1. Só o módulo + ESP, **sem a fonte**: ponha um fio entre os pinos **2 e 3** do
   DB9 do módulo e, no REPL (`mpremote connect /dev/ttyACM0 repl`):
   ```python
   import selftest
   selftest.loopback()
   ```
   Deve imprimir `OK` para um dos sentidos e dizer se há fios invertidos.
2. Ligue o cabo com o pino 6 em +5 V (1 kΩ) e o pino 4 solto, fonte em
   `RS-232 / 9600 / NONE 8 BITS`, e rode `selftest.probe()`. Ele varre
   baud/stop-bits/sentido até a fonte responder ao `*IDN?` e imprime os valores
   para o `config.py`.
3. Ou rode `selftest.labtest()`, que repete a abertura do Lab Test.
4. Só então rode o `main.py` e use o app.

## Configuração RS-232 na fonte (painel frontal)

No menu **I/O Config** da fonte selecione:

1. `RS-232` (em vez de HPIB/488)
2. `9600 BAUD` (padrão; se mudar, ajuste `BAUD` em `firmware/config.py`)
3. `NONE 8 BITS` (paridade nenhuma, 8 bits)

A fonte salva isso em memória não volátil.

## Diagrama de ligação

```
 ESP32-C3 Super Mini          Módulo MAX3232               E3634A (DB9 macho traseiro, DTE)
 ┌──────────────┐            ┌───────────────┐            ┌───────────────────────────┐
 │ 3V3 ─────────┼────────────┤ VCC           │            │                           │
 │ GND ─────────┼────────────┤ GND ──────────┼────────────┤ 5  GND                    │
 │ GPIO21 (TX) ─┼────────────┤ T1IN   T1OUT ─┼────────────┤ 2  RXD  (entrada da fonte)│
 │ GPIO20 (RX) ─┼────────────┤ R1OUT  R1IN  ─┼────────────┤ 3  TXD  (saída da fonte)  │
 │              │            └───────────────┘            │                           │
 │ GPIO8 = LED  │                             (solto) ┤ 4  DTR  (saída)           │
 │ 5V ──[1 kΩ]──┼─────────────────────────────────────────┤ 6  DSR  (entrada)         │
 └──────────────┘                                         │                           │
                                                          └───────────────────────────┘
```

### Protocolo RS-232 da fonte (manual da família E363x)

O que o manual diz (trechos do guia da E3631A/E3633A/E3634A; **não consegui abrir os PDFs do
fabricante neste ambiente**, então confira na seção *RS-232 Interface* do guia
`E3634-90001`):

- a fonte é um **DTE** e faz o handshake por **DTR (pino 4)** e **DSR (pino 6)**;
- ela **monitora o DSR antes de enviar cada caractere**: com o DSR falso, a **saída fica
  suspensa**; ela também **segura o DTR em falso enquanto a saída está suspensa**;
- o travamento (*interface deadlock*) se desfaz com **`<Ctrl-C>`** (0x03), que descarta a saída
  pendente;
- para desativar o handshake: não ligar o DTR e **ligar o DSR a nível TRUE**;
- por RS-232 é preciso mandar **`SYST:REM`** antes dos demais comandos;
- velocidade 300 a 9600 (padrão **9600**); paridade/bits: **None 8 bits** (padrão), Odd 7 bits ou
  Even 7 bits;
- o cabo que acompanha a fonte é 9 pinos fêmea ↔ fêmea (cruzado, para ligar a um PC).

O manual também diz quando a fonte põe o **DTR em falso**:

- quando o buffer de entrada enche (≈100 caracteres);
- **quando ela tem uma resposta para mandar** (processou uma query e recebeu o `<new line>`);
  ela volta a pôr o DTR em verdadeiro só depois de enviar a resposta.

Consequências no projeto:

1. **Sem DSR verdadeiro a fonte aceita comandos (o display mostra `RMT`) mas nunca responde.**
   É o sintoma que vimos (`timeout rx=vazio`).
2. **O jumper 4↔6 causa exatamente isso.** Com ele, o DSR copia o DTR da própria fonte. Ao ter
   uma resposta pronta ela põe o DTR em falso, o DSR cai junto, a saída fica suspensa e o DTR
   continua falso: travamento a cada query. Comandos sem resposta (`SYST:REM`) funcionam; queries
   não. Os comandos seguintes ainda geram erro (`-410 Query INTERRUPTED`) e a fonte apita.
   No PC isso não acontece porque o DSR da fonte vem do DTR do PC, que fica sempre verdadeiro.
3. Ligação correta com o módulo de 4 pinos: **pino 4 solto** e **pino 6 em +5 V por 1 kΩ**
   (do pad `5V` do ESP). A entrada DSR tem ~5 kΩ, então o pino 6 fica em cerca de +4 V, nível
   verdadeiro. 3,3 V fica no limite do receptor RS-232; use os 5 V.
4. O firmware abre a sessão com `<Ctrl-C>` (descarta uma resposta presa), `*CLS`, esvazia
   `SYST:ERR?`, `*IDN?` e só então `SYST:REM`, como o Lab Test; comandos terminam em `\r\n`.
5. O manual sugere baud menor (300/600/1200) com o handshake desligado, para não estourar o
   buffer de entrada. Os comandos do firmware são curtos e cada query espera a resposta, então
   9600 deve servir; se aparecer `-363` (overrun) na fila de erros, use 1200 na fonte e no `config.py`.
6. Depois de mudar a ligação, **desligue e ligue a fonte** uma vez.
7. Para conferir: com a fonte ligada e o cabo conectado, meça entre o pino 5 e o pino 6 no DB9 da
   fonte: **cerca de +4 V** = DSR verdadeiro.

### Opcional: handshake completo com o 2º canal do MAX3232

Se o seu módulo expõe T2/R2:

| ESP32 | MAX3232 | E3634A |
|---|---|---|
| GPIO (ex. 3) ← | R2OUT ← R2IN | ← pino 4 (DTR) |
| GPIO (ex. 4) → | T2IN → T2OUT | → pino 6 (DSR) |

e em `config.py`: `DTR_IN_PIN = 3`, `DSR_OUT_PIN = 4`. O firmware espera o
DTR da fonte antes de cada envio (evita estourar o buffer dela).

## Atenção ao "cruzamento" TX/RX do módulo

Módulos MAX3232 com DB9 vêm ligados como **DCE** ou **DTE** — varia de
fabricante. A E3634A é **DTE**. A regra que não falha:

1. Com a fonte ligada e nada conectado, meça com multímetro entre o pino 3
   e o pino 5 do DB9 **da fonte**: deve dar ≈ **−5 a −12 V** (TX em repouso).
2. O pino da fonte que tem essa tensão negativa (TXD, pino 3) tem que ir no
   **R1IN** do MAX3232; o pino 2 da fonte vai no **T1OUT**.
3. Se você ligar o DB9 do módulo direto e as duas pontas mostrarem tensão
   negativa no mesmo pino, são duas saídas se enfrentando → inverta 2↔3.

O mais seguro é ignorar o DB9 do módulo e montar um cabo curto com um
**DB9 fêmea de solda**: pinos 2, 3, 5 até o módulo, pino 6 em +5 V por 1 kΩ e
pino 4 solto.

## Alimentação

- Super Mini via USB-C 5 V; o MAX3232 pega 3,3 V do pino `3V3` da placa.
- Não use o 5 V no VCC do MAX3232: o R1OUT sairia em 5 V e o GPIO20 do
  ESP32-C3 **não é tolerante a 5 V**.
- O GND do RS-232 da E3634A é o terra do chassi/interface — o terminal de
  saída da fonte é flutuante, então não há laço de terra com a carga.

## LED azul = atividade da serial (GPIO8)

O LED pisca a cada byte trocado com a fonte, para validar a comunicação sem console:

| LED | Significado |
|---|---|
| Flash **curto** (40 ms) | O ESP enviou um comando à fonte (TX) |
| Flash **longo** (200 ms) | A fonte **respondeu** (RX chegou) |
| Só flashes curtos, nunca longos | A fonte recebe os comandos mas não responde (o caso a depurar) |
| Apagado em repouso | App conectado e nada trafegando |
| Piscada de 50 ms a cada 2 s | Nenhum app conectado (BLE anunciando) |

Os testes do `selftest.py` que usam a UART (`loopback()`, `diag()`, `probe()`, `beep()`, `labtest()`)
usam o mesmo padrão. Em um loopback correto você vê um flash curto seguido de um longo.
