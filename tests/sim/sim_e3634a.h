/* Simulador da Agilent E3634A na interface RS-232 (nivel de bytes).
 *
 * Implementa o subconjunto SCPI do firmware, uma carga resistiva (CV/CC) e o
 * comportamento eletrico do handshake descrito no manual:
 *   - a fonte so transmite com o DSR (pino 6) verdadeiro e confere o DSR antes
 *     de cada caractere;
 *   - ao ter uma resposta para mandar, ela poe o DTR (pino 4) em FALSO e volta
 *     a TRUE depois de mandar;
 *   - <Ctrl-C> descarta a saida pendente;
 *   - comando novo chegando com resposta ainda presa -> -410 Query INTERRUPTED + beep.
 */
#ifndef SIM_E3634A_H
#define SIM_E3634A_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WIRE_DSR_PULLUP,   /* pino 6 em +5 V por 1 kOhm, pino 4 solto (correto) */
    WIRE_JUMPER_4_6,   /* jumper 4-6: DSR segue o DTR da propria fonte (trava) */
    WIRE_DSR_OPEN,     /* pino 6 solto: DSR falso, a fonte nunca transmite */
} sim_wiring_t;

#define SIM_IDN "HEWLETT-PACKARD,E3634A,0,2.4-6.1-2.1"

typedef struct {
    /* configuracao do teste */
    sim_wiring_t wiring;
    bool alive;          /* false: fonte desligada/cabo solto */
    bool flow;           /* responde envolvendo XOFF/XON */
    bool garbage;        /* simula baud/paridade errados */
    bool compound_ok;
    double load_ohm;

    /* linhas */
    bool dtr;            /* saida da fonte (pino 4) */

    /* fila serial */
    char in[512];
    size_t in_len;
    uint8_t out[1024];
    size_t out_len;

    /* estado do instrumento */
    bool remote;
    char range[8];
    double vset, iset;
    bool out_on;
    double ovp, ocp;
    bool ovp_en, ocp_en, ovp_trip, ocp_trip;
    bool disp;
    char text[16];
    char trig_src[8];
    double trig_del, vtrig, itrig;
    struct { bool used; char range[8]; double vset, iset, ovp, ocp; bool ovp_en, ocp_en; } mem[6];
    int errq[32];
    int errn;

    /* observacao */
    char log[256][96];    /* comandos recebidos (linhas) */
    int log_n;
    uint8_t raw[4096];   /* bytes crus recebidos */
    size_t raw_len;
    int cleared;         /* quantos <Ctrl-C> */
    int beeps;           /* beeps de erro (-410 etc.) */
} sim_t;

void sim_init(sim_t *s);
void sim_reset_state(sim_t *s);
/* host -> fonte */
void sim_write(sim_t *s, const uint8_t *d, size_t n);
/* fonte -> host: so devolve bytes enquanto o DSR estiver verdadeiro */
size_t sim_read(sim_t *s, uint8_t *d, size_t max);
bool sim_dsr(const sim_t *s);
int sim_find_log(const sim_t *s, const char *cmd);
bool sim_has_error(const sim_t *s, int code);

#endif
