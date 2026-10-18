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
 │ GPIO8 = LED  │                                ┌────────┤ 4  DTR  (saída)           │
 └──────────────┘                     jumper ─── │        │                           │
                                                 └────────┤ 6  DSR  (entrada)         │
                                                          └───────────────────────────┘
```

### Por que o jumper 4 ↔ 6 (DTR → DSR)?

A E363x usa handshake por hardware **DTR/DSR**: ela **só transmite** se a linha
**DSR (pino 6)** estiver ativa. Sem isso a fonte recebe e executa os comandos,
mas **nunca responde às queries** (sintoma: `timeout` / LED do ESP piscando
rápido, display da fonte mostrando `RMT`).

Ligando o DTR da própria fonte (pino 4) no DSR (pino 6), o DSR fica ativo
sempre que ela está pronta. É o método recomendado e é o padrão do firmware.

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
**DB9 fêmea de solda**: pinos 2, 3, 5 até o módulo + jumper 4–6 no próprio
conector.

## Alimentação

- Super Mini via USB-C 5 V; o MAX3232 pega 3,3 V do pino `3V3` da placa.
- Não use o 5 V no VCC do MAX3232: o R1OUT sairia em 5 V e o GPIO20 do
  ESP32-C3 **não é tolerante a 5 V**.
- O GND do RS-232 da E3634A é o terra do chassi/interface — o terminal de
  saída da fonte é flutuante, então não há laço de terra com a carga.

## LED de status (GPIO8)

| LED | Significado |
|---|---|
| Pisca lento (1 s) | Aguardando o app conectar via BLE |
| Pisca rápido | App conectado, mas a fonte **não responde** (cabo/baud/jumper) |
| Aceso fixo | App conectado e fonte em comunicação |
