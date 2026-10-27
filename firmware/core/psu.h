/* Driver SCPI para a fonte Agilent/Keysight E3634A via RS-232.
 *
 * Portavel: nao depende do ESP-IDF. A placa (ou o PC, ou o simulador) entrega
 * as funcoes de E/S em psu_io_t.
 *
 * Regras de comunicacao (manual E3634A, "RS-232 Interface Configuration"):
 *   - 9600 baud, 8 bits, sem paridade, 2 stop bits; comandos terminam em "\r\n";
 *     respostas terminam em "\r\n" (aceita "\n" ou "\r")
 *   - a fonte e DTE e usa DTR (pino 4) / DSR (pino 6): so transmite com o DSR
 *     verdadeiro. Ao ter resposta para mandar ela poe o DTR em FALSO; por isso o
 *     DSR NAO pode vir do proprio DTR (jumper 4-6 trava a fonte). Sem handshake:
 *     pino 6 em +5 V por 1 kOhm e pino 4 solto
 *   - <Ctrl-C> (0x03) cancela a operacao e descarta a saida pendente (= device
 *     clear do GPIB): toda sessao comeca com ele
 *   - por RS-232 e preciso SYST:REM (ou SYST:RWL) antes de programar a fonte
 *   - a escrita NAO limpa a entrada (descartar resposta em transito gera -410
 *     Query INTERRUPTED e beep); so a query limpa
 */
#ifndef PSU_H
#define PSU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *ctx;
    void (*write)(void *ctx, const uint8_t *d, size_t n);
    size_t (*read)(void *ctx, uint8_t *d, size_t max);    /* nao bloqueante */
    uint32_t (*now_ms)(void *ctx);
    void (*sleep_ms)(void *ctx, uint32_t ms);
    int (*dtr_ok)(void *ctx);              /* opcional: 1 = fonte pronta p/ receber */
    void (*activity)(void *ctx, bool rx);  /* opcional: LED de atividade */
    void (*trace)(void *ctx, char dir, const char *line); /* opcional: '>' / '<' / '!' */
} psu_io_t;

typedef enum {
    PSU_OK = 0,
    PSU_ELINK = -1,   /* fonte nao respondeu / resposta ilegivel (psu->err) */
    PSU_ESCPI = -2,   /* fonte reportou erro (psu->scpi_code / scpi_msg) */
    PSU_EARG = -3,    /* argumento invalido (psu->err) */
} psu_rc_t;

typedef struct {
    double v, i;
    bool out, ovp_t, ocp_t;
} psu_meas_t;

typedef struct {
    char range[8];
    double vset, iset;
    bool out;
    double ovp;
    bool ovp_en;
    double ocp;
    bool ocp_en;
    bool disp;
    char trig_src[8];
    double trig_del, vtrig, itrig;
} psu_status_t;

typedef struct {
    psu_io_t io;
    uint32_t timeout_ms;
    bool xonxoff;
    bool compound;
    bool paused;            /* fonte mandou XOFF */
    int state_every, state_n;
    bool st_out, st_ovp_t, st_ocp_t;
    uint8_t rx[512];
    size_t rx_len;
    char idn[96];
    char err[96];
    int scpi_code;
    char scpi_msg[96];
    uint32_t min_gap_ms;    /* pausa minima entre dois envios (a fonte perde bytes sem isso) */
    uint32_t last_tx_ms;
    bool has_last_tx;
} psu_t;

/* faixa: limites programaveis (manual, "Programming Ranges") */
#define PSU_P25V_VMAX 25.75
#define PSU_P25V_IMAX 7.21
#define PSU_P50V_VMAX 51.5
#define PSU_P50V_IMAX 4.12
#define PSU_OVP_MIN 1.0
#define PSU_OVP_MAX 55.0
#define PSU_OCP_MIN 0.0
#define PSU_OCP_MAX 7.5
#define PSU_MEM_SLOTS 5

void psu_init(psu_t *p, const psu_io_t *io, uint32_t timeout_ms);

/* baixo nivel */
int psu_query(psu_t *p, const char *cmd, char *out, size_t n, uint32_t timeout_ms);
int psu_write(psu_t *p, const char *cmd, bool check);
int psu_read_error(psu_t *p, int *code, char *msg, size_t n, uint32_t timeout_ms);
/* Esvazia SYST:ERR? (max. 20). cb e chamado para cada erro; retorna PSU_OK ou erro de link. */
int psu_errors(psu_t *p, void (*cb)(void *arg, int code, const char *msg), void *arg,
               uint32_t timeout_ms);
int psu_check_error(psu_t *p);
void psu_device_clear(psu_t *p);

/* sessao */
int psu_connect(psu_t *p, bool lock_panel, bool labtest);
int psu_ping(psu_t *p, char *out, size_t n);
int psu_remote(psu_t *p, bool lock_panel);
int psu_local(psu_t *p);

/* operacao */
int psu_measure(psu_t *p, psu_meas_t *m);
int psu_status(psu_t *p, psu_status_t *st);
int psu_output(psu_t *p, bool on);
int psu_set_range(psu_t *p, const char *rng);
int psu_set_voltage(psu_t *p, double v);
int psu_set_current(psu_t *p, double a);
int psu_apply(psu_t *p, double v, double a);
int psu_set_ovp(psu_t *p, const double *level, const bool *enabled);
int psu_set_ocp(psu_t *p, const double *level, const bool *enabled);
int psu_clear_protection(psu_t *p);
int psu_save(psu_t *p, int n);
int psu_recall(psu_t *p, int n);
int psu_display(psu_t *p, bool on);
int psu_display_text(psu_t *p, const char *text);
int psu_display_text_clear(psu_t *p);
int psu_beep(psu_t *p);
int psu_reset(psu_t *p);
int psu_trigger_source(psu_t *p, const char *src);
int psu_trigger_delay(psu_t *p, double s);
int psu_trigger_levels(psu_t *p, const double *v, const double *a);
int psu_initiate(psu_t *p);
int psu_trigger(psu_t *p);

/* utilidades (tambem usadas pelo bridge) */
const char *psu_norm_range(const char *rng);   /* "LOW"->"P25V"...; NULL se invalida */
void psu_range_limits(const char *rng, double *vmax, double *imax);

#endif
