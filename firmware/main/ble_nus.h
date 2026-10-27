/* Periferico BLE com Nordic UART Service (NUS), NimBLE.
 *   RX (app -> ESP): escrita na caracteristica 6E400002
 *   TX (ESP -> app): notificacao na caracteristica 6E400003
 * Cada mensagem e uma linha terminada em '\n'. */
#ifndef BLE_NUS_H
#define BLE_NUS_H

#include <stdbool.h>
#include <stddef.h>

typedef enum { NUS_EV_CONNECT = 1, NUS_EV_DISCONNECT = 2 } nus_event_t;

void nus_init(const char *name);
bool nus_connected(void);
/* Nao bloqueante. Retorna o tamanho da linha (sem '\n'), 0 se nao ha. */
size_t nus_read_line(char *buf, size_t cap);
/* Retorna true e preenche *ev se houve conexao/desconexao. */
bool nus_poll_event(nus_event_t *ev);
/* Envia uma linha (acrescenta '\n'), fatiada pelo MTU. */
void nus_send_line(const char *line);

#endif
