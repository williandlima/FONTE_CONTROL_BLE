/* Pinos da ESP32-C3 Super Mini (USB-C para a direita, ver docs/HARDWARE.md). */
#ifndef BOARD_H
#define BOARD_H

#define PSU_UART_NUM UART_NUM_1
#define PSU_TX_GPIO 21     /* GPIO21 -> T1IN do MAX3232 -> pino 2 (RX) da fonte */
#define PSU_RX_GPIO 20     /* GPIO20 <- R1OUT do MAX3232 <- pino 3 (TX) da fonte */

/* Handshake opcional: so com modulo MAX3232 de 2 canais ligado aos pinos 4/6.
 * Com o modulo de 4 pinos deixe -1: pino 6 da fonte em +5 V por 1 kOhm, pino 4 solto. */
#define PSU_DTR_IN_GPIO -1   /* le o DTR da fonte (R2OUT): fonte pronta p/ receber */
#define PSU_DSR_OUT_GPIO -1  /* aciona o DSR da fonte (T2IN): ESP pronto p/ receber */

#define LED_GPIO 8           /* LED azul da Super Mini */
#define LED_ACTIVE_LOW 1

#endif
