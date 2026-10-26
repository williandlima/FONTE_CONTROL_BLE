# Hardware e ligações

## Lista de material

| Item | Observação |
|---|---|
| ESP32-C3 Super Mini | BLE 5, USB-C nativo (USB-CDC), 3,3 V |
| Módulo conversor RS-232 ↔ TTL **MAX3232** | Precisa funcionar em **3,3 V** (MAX232 "puro" é só 5 V — não use) |
| Conector DB9 **fêmea** (solda) ou cabo | Para encaixar no DB9 macho traseiro da E3634A |
| Fonte 5 V USB-C (carregador de celular) | Alimenta o ESP32 e o MAX3232 |

> Modelo assumido: **Agilent/Keysight E3634A** (25 V/7 A e 50 V/4 A, RS-232 + GPIB).
> As irmãs E3632A/E3633A usam o mesmo conjunto SCPI (mudam só os limites
> `PSU_P25V_*`/`PSU_P50V_*` em `firmware/core/psu.h`).

## Seu hardware (conferido pela foto)

- **ESP32-C3 Super Mini**: GPIO21 e GPIO20 são os dois primeiros pinos da fileira
  superior (lado oposto ao 5V); `3V3`, `G` e `5V` ficam juntos junto ao USB-C.
- **Módulo RS-232 azul** (U1 de 16 pinos, capacitores C1–C5, LED D1, DB9 "9P"):
  header de 4 pinos **GND, TX, RX, VCC** — só o canal 1 (T1/R1). Não existe
  DTR/DSR/RTS/CTS no módulo, então `PSU_DTR_IN_GPIO`/`PSU_DSR_OUT_GPIO` ficam
  `-1` (`firmware/main/board.h`), e **duas** linhas de controle da fonte
  precisam ser forçadas em nível verdadeiro com resistor: **pino 6 (DSR)** e
  **pino 8 (CTS)**. Os pinos 4 (DTR) e 7 (RTS) ficam soltos.

| Módulo | Super Mini |
|---|---|
| VCC | **3V3** (nunca 5V) |
| GND | G |
| TX | GPIO21 |
| RX | GPIO20 |
| pino 6 do DB9 (DSR) | **5V por resistor de 1 kΩ** |
| pino 8 do DB9 (CTS) | **5V por resistor de 1 kΩ** |

Um cabo null-modem de fábrica (o que vem com o instrumento, ou o que o Lab
Test usa no PC) resolve isso sozinho: ele cruza **4↔6 e 7↔8**, e tanto o DTR
quanto o RTS do computador ficam sempre em nível alto quando a porta abre.
Com o módulo de 4 pinos essas duas linhas não existem, então **as duas**
precisam do resistor — só o DSR não é suficiente.

A rotulagem TX/RX desses módulos varia. Não adivinhe: rode o autoteste
`loop` no console (abaixo) e ele diz se a ida e a volta funcionam.

### Três coisas a conferir antes de ligar na fonte

1. **Marcação do chip U1.** Tem que ser **MAX3232** (ou SP3232/ST3232/
   ADM3232). Se for **MAX232** puro, ele só funciona em 5V e a saída R1OUT
   chegaria a 5V no GPIO20 do ESP32-C3 — não ligue.
2. **Gênero e tipo do DB9.** Esses módulos costumam ser fiados como **DTE**
   (igual a um PC: TXD no pino 3, RXD no pino 2). A E3634A também é DTE, então
   entre as duas pontas o cabo precisa ser **null-modem (cruzado)**: 2↔3 e 5↔5.
   Um cabo reto/extensor não funciona. Se os gêneros dos conectores não
   encaixam, use um adaptador de gênero ou faça um cabo curto.
3. **Pino 6 e pino 8 em +5 V por 1 kΩ cada; pinos 4 e 7 soltos; sem jumper 4↔6** (ver abaixo).

### Diagnóstico pelo console USB

O firmware em C mostra no console USB (VS Code → PlatformIO → **Monitor**, 115200) cada linha
SCPI enviada (`>`) e recebida (`<`), e aceita comandos (digite `help`):

| Comando | O que faz |
|---|---|
| `loop` | Loopback: com um fio entre os pinos **2 e 3** do DB9 do módulo (sem a fonte), o que sai tem que voltar |
| `diag` | Manda `<Ctrl-C>` + `*IDN?` e mostra **todos os bytes crus** que voltarem em 1,5 s |
| `edges` | Conta pulsos elétricos no pino RX, sem decodificar nada — não depende de baud/paridade, só diz se chegou alguma coisa |
| `*IDN?`, `MEAS:VOLT?`... | Qualquer SCPI: com `?` mostra a resposta; sem `?` confere `SYST:ERR?` |
| `link` | Nova tentativa de abrir a sessão com a fonte |
| `baud 1200` | Muda a velocidade (tem que ser igual à da fonte) |
| `{"cmd":"meas"}` | Qualquer comando do app, sem precisar do celular |
| `trace` | Liga/desliga o log das linhas SCPI |

Como ler o `diag`:

- **0 bytes recebidos:** a fonte não transmite: DSR (pino 6) ou CTS (pino 8) sem +5 V, jumper
  4↔6, fio do pino 3, ou fonte em HPIB. Se o display mostra `RMT` depois de `link`, a ida
  funciona e falta a volta.
- **Bytes sem texto legível:** baud/paridade diferentes da fonte (menu I/O Config).
- **`HEWLETT-PACKARD,E3634A,...`:** a serial está perfeita.

Se o `diag` continuar em 0 bytes mesmo com DSR em +5 V, interface em RS-232 e fonte religada,
rode `edges`. Ele não usa a UART: só fica lendo o nível do pino RX direto do GPIO, 20 vezes por
segundo, e conta quantas vezes ele mudou de nível. Isso tira qualquer dúvida de baud/paridade da
lista de suspeitos:

- **"NADA chega ao pino RX"** (0 ou poucas transições): o sinal elétrico da fonte simplesmente não
  está chegando no ESP. Não é mais baud nem firmware — confira (nesta ordem) **o pino 8 (CTS) em
  +5 V por 1 kΩ** (não só o 6), o fio do pino 3 do cabo (TXD da fonte), o GND, e se o pino 6
  (DSR) está mesmo no pino certo do conector (não em um pino vizinho por engano); religue a fonte.
- **"HA ATIVIDADE no pino RX"** (muitas transições): o sinal chega, só não decodifica — aí sim é
  baud/paridade/nível, confira o menu da fonte e tente `baud 1200` ou `baud 2400`.

Obs.: no boot, a ROM do ESP32‑C3 imprime algumas linhas em 115200 no GPIO21 (antes do firmware
assumir o pino). A fonte pode apitar **uma vez ao ligar/resetar o ESP**; a sessão seguinte limpa a
fila de erros. O console do firmware fica no USB, não no GPIO20/21.

### Teste sem o ESP (PC + adaptador USB-RS232, como o Lab Test)

`host/bin/pc_bridge.exe COM5` roda **o mesmo código C do firmware** no PC. Se lá a fonte
responde e no ESP não, o firmware está certo e a diferença é elétrica (pino 6, jumper, módulo).

### Roteiro de teste (nesta ordem)

1. Só o módulo + ESP, **sem a fonte**: fio entre os pinos **2 e 3** do DB9 do módulo e, no
   console, `loop` → deve dar `OK`.
2. Ligue o cabo com os pinos 6 e 8 em +5 V (1 kΩ cada) e os pinos 4 e 7 soltos, fonte em
   `RS-232 / 9600 / NONE 8 BITS`, **desligue e ligue a fonte**, e rode `diag`.
3. Com o `diag` mostrando o `*IDN?`, use `link` (ou espere a reconexão automática) e o app.

## Configuração RS-232 na fonte (painel frontal)

No menu **I/O Config** da fonte selecione:

1. `RS-232` (em vez de HPIB/488)
2. `9600 BAUD` (padrão; se mudar, ajuste `PSU_BAUD` em `firmware/core/config.h` ou use `baud N` no console)
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
 │ 5V ──[1 kΩ]──┼─────────────────────────────────────────┤ 8  CTS  (entrada)         │
 │              │                             (solto) ┤ 7  RTS  (saída)           │
 └──────────────┘                                         │                           │
                                                          └───────────────────────────┘
```

### Protocolo RS-232 da fonte (manual da família E363x)

Resumo completo, com os comandos usados e as fontes, em [SCPI_E3634A.md](SCPI_E3634A.md).

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

Além do DSR, há indicação (nota de aplicação da própria Agilent sobre RS-232, não específica da
E3634A, então trate como reforço e não como regra garantida) de que **instrumentos Agilent em
geral também exigem o CTS (pino 8) em nível verdadeiro** para se comunicar — uma segunda linha
de handshake (RTS/CTS), independente do DTR/DSR. Um cabo null-modem de fábrica cruza **as duas**
(4↔6 e 7↔8), então ela nunca aparece como problema por lá; um módulo de 4 pinos não tem nem uma
nem outra, e só forçar o DSR pode não ser suficiente — ver item 3 abaixo.

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
3. Ligação correta com o módulo de 4 pinos: **pino 4 e pino 7 soltos**, **pino 6 (DSR) e pino 8
   (CTS) em +5 V por 1 kΩ cada** (do pad `5V` do ESP). A entrada tem ~5 kΩ, então cada pino fica
   em cerca de +4 V, nível verdadeiro. 3,3 V fica no limite do receptor RS-232; use os 5 V. Se
   você já tinha corrigido só o DSR e a fonte continuou muda, o CTS solto é a causa mais provável.
4. O firmware abre a sessão com `<Ctrl-C>` (descarta uma resposta presa), `*CLS`, esvazia
   `SYST:ERR?`, `*IDN?` e só então `SYST:REM`, como o Lab Test; comandos terminam em `\r\n`.
5. O manual sugere baud menor (300/600/1200) com o handshake desligado, para não estourar o
   buffer de entrada. Os comandos do firmware são curtos e cada query espera a resposta, então
   9600 deve servir; se aparecer `-363` (overrun) na fila de erros, use 1200 na fonte e em `PSU_BAUD`.
6. Depois de mudar a ligação, **desligue e ligue a fonte** uma vez.
7. Para conferir: com a fonte ligada e o cabo conectado, meça entre o pino 5 e o pino 6, e depois
   entre o pino 5 e o pino 8, no DB9 da fonte: **cerca de +4 V** nos dois = DSR e CTS verdadeiros.

### Opcional: handshake completo com o 2º canal do MAX3232

Se o seu módulo expõe T2/R2:

| ESP32 | MAX3232 | E3634A |
|---|---|---|
| GPIO (ex. 3) ← | R2OUT ← R2IN | ← pino 4 (DTR) |
| GPIO (ex. 4) → | T2IN → T2OUT | → pino 6 (DSR) |

e em `firmware/main/board.h`: `PSU_DTR_IN_GPIO 3`, `PSU_DSR_OUT_GPIO 4`. O firmware espera o
DTR da fonte antes de cada envio (evita estourar o buffer dela) e mantém o DSR verdadeiro
(o MAX3232 inverte: nível TTL baixo = linha RS-232 verdadeira).

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
**DB9 fêmea de solda**: pinos 2, 3, 5 até o módulo, pinos 6 e 8 em +5 V por
1 kΩ cada, pinos 4 e 7 soltos.

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

Os comandos de console `link`, `*IDN?` etc. usam o mesmo padrão: um flash curto seguido de um
longo quer dizer comando enviado e resposta recebida.
