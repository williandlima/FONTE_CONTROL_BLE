/* Protocolo app <-> firmware: uma mensagem JSON por linha ('\n').
 * Especificacao completa em docs/PROTOCOLO.md. */
#ifndef BRIDGE_H
#define BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "psu.h"

typedef struct {
    psu_t *psu;
    void (*send)(void *ctx, const char *line);   /* envia uma linha (sem '\n') ao app */
    void *send_ctx;
    uint32_t poll_ms, poll_min_ms, reconnect_ms;
    bool lock_panel, labtest;
    bool link;              /* fonte respondendo */
    bool client;            /* app conectado via BLE */
    psu_status_t state;
    bool has_state;
    uint32_t t_poll, t_link;
    bool t_link_set;
    bool need_status;
    int fails;              /* tentativas de conexao seguidas sem sucesso */
    int errs;               /* falhas seguidas com o link ativo */
    char last_err[96];
} bridge_t;

void bridge_init(bridge_t *b, psu_t *psu, void (*send)(void *, const char *), void *ctx);
void bridge_on_client(bridge_t *b, bool connected);
void bridge_handle_line(bridge_t *b, const char *line);
void bridge_tick(bridge_t *b);
bool bridge_try_link(bridge_t *b);

#endif
