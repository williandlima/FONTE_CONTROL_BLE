#include "psu.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#define XON 0x11
#define XOFF 0x13

/* comparacao sem caixa (strcasecmp nao e C padrao) */
static bool ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
    return *a == *b;
}

static uint32_t now(psu_t *p) { return p->io.now_ms(p->io.ctx); }
static void nap(psu_t *p, uint32_t ms) { p->io.sleep_ms(p->io.ctx, ms); }
static uint32_t since(psu_t *p, uint32_t t0) { return now(p) - t0; }

static void trace(psu_t *p, char dir, const char *s)
{
    if (p->io.trace) p->io.trace(p->io.ctx, dir, s);
}

static int link_err(psu_t *p, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->err, sizeof p->err, fmt, ap);
    va_end(ap);
    trace(p, '!', p->err);
    return PSU_ELINK;
}

static int arg_err(psu_t *p, const char *msg)
{
    snprintf(p->err, sizeof p->err, "%s", msg);
    return PSU_EARG;
}

/* bytes -> texto legivel para mensagens de erro ("\xff\xfe...") */
static void esc_bytes(char *out, size_t cap, const uint8_t *d, size_t n)
{
    size_t k = 0;
    if (n > 16) n = 16;
    for (size_t i = 0; i < n && k + 5 < cap; i++) {
        if (d[i] >= 0x20 && d[i] < 0x7f && d[i] != '\\')
            out[k++] = (char)d[i];
        else
            k += (size_t)snprintf(out + k, cap - k, "\\x%02x", d[i]);
    }
    out[k < cap ? k : cap - 1] = '\0';
}

void psu_init(psu_t *p, const psu_io_t *io, uint32_t timeout_ms)
{
    memset(p, 0, sizeof *p);
    p->io = *io;
    p->timeout_ms = timeout_ms;
    p->xonxoff = true;
    p->state_every = 5;
    p->min_gap_ms = PSU_MIN_GAP_MS;
}

/* ------------------------------------------------------------ baixo nivel */

/* Move bytes da UART para rx[], tratando XON/XOFF. Retorna true se chegou algo. */
static bool pull(psu_t *p)
{
    uint8_t tmp[128];
    size_t n = p->io.read(p->io.ctx, tmp, sizeof tmp);
    if (!n) return false;
    if (p->io.activity) p->io.activity(p->io.ctx, true);
    for (size_t i = 0; i < n; i++) {
        uint8_t c = tmp[i];
        if (p->xonxoff && (c == XON || c == XOFF)) {
            p->paused = (c == XOFF);
            continue;
        }
        if (p->rx_len >= sizeof p->rx) {           /* fluxo sem fim: guarda so o final */
            memmove(p->rx, p->rx + p->rx_len - 64, 64);
            p->rx_len = 64;
        }
        p->rx[p->rx_len++] = c;
    }
    return true;
}

static void flush_input(psu_t *p)
{
    uint32_t t0 = now(p);
    while (pull(p))
        if (since(p, t0) > 100) break;   /* linha despejando dados sem parar */
    p->rx_len = 0;
}

static int send_line(psu_t *p, const char *line)
{
    if (p->io.dtr_ok) {
        uint32_t t0 = now(p);
        while (!p->io.dtr_ok(p->io.ctx)) {
            if (since(p, t0) > p->timeout_ms) return link_err(p, "DTR da fonte inativo");
            nap(p, 2);
        }
    }
    if (p->xonxoff) pull(p);            /* pega XOFF/XON ainda na fila */
    if (p->paused) {
        uint32_t t0 = now(p);
        while (p->paused) {
            pull(p);
            if (since(p, t0) > p->timeout_ms) return link_err(p, "fonte em XOFF");
            nap(p, 2);
        }
    }
    /* pausa minima entre dois envios: comandos muito colados perdem bytes na fonte */
    if (p->has_last_tx) {
        uint32_t gap = since(p, p->last_tx_ms);
        if (gap < p->min_gap_ms) nap(p, p->min_gap_ms - gap);
    }
    trace(p, '>', line);
    p->io.write(p->io.ctx, (const uint8_t *)line, strlen(line));
    p->io.write(p->io.ctx, (const uint8_t *)"\r\n", 2);
    if (p->io.activity) p->io.activity(p->io.ctx, false);
    p->last_tx_ms = now(p);
    p->has_last_tx = true;
    return PSU_OK;
}

static int read_line(psu_t *p, char *out, size_t n, uint32_t timeout_ms)
{
    uint32_t t0 = now(p);
    if (!timeout_ms) timeout_ms = p->timeout_ms;
    for (;;) {
        size_t i;
        for (i = 0; i < p->rx_len; i++)
            if (p->rx[i] == '\n' || p->rx[i] == '\r') break;
        if (i < p->rx_len) {
            size_t a = 0, b = i;
            while (a < b && (p->rx[a] == ' ' || p->rx[a] == 0)) a++;
            while (b > a && (p->rx[b - 1] == ' ' || p->rx[b - 1] == 0)) b--;
            bool bad = false;
            for (size_t k = a; k < b; k++)
                if (p->rx[k] >= 0x80 || (p->rx[k] < 0x20 && p->rx[k] != '\t')) bad = true;
            char tmp[48];
            if (bad) esc_bytes(tmp, sizeof tmp, p->rx + a, b - a);
            size_t len = b - a;
            if (!bad && len) {
                if (len >= n) len = n - 1;
                memcpy(out, p->rx + a, len);
                out[len] = '\0';
            }
            memmove(p->rx, p->rx + i + 1, p->rx_len - i - 1);
            p->rx_len -= i + 1;
            if (bad) {                         /* lixo: baud/paridade errados */
                p->rx_len = 0;
                return link_err(p, "rx invalido '%s'", tmp);
            }
            if (!len) continue;                /* sobra do \r\n */
            trace(p, '<', out);
            return PSU_OK;
        }
        /* o tempo limite vale mesmo com dados chegando (fluxo continuo/ruido) */
        if (since(p, t0) > timeout_ms) {
            char tmp[80];
            if (p->rx_len) {
                esc_bytes(tmp, sizeof tmp, p->rx, p->rx_len);
                p->rx_len = 0;
                return link_err(p, "timeout rx='%s'", tmp);
            }
            return link_err(p, "timeout rx=vazio");
        }
        if (pull(p)) continue;
        nap(p, 2);
    }
}

int psu_query(psu_t *p, const char *cmd, char *out, size_t n, uint32_t timeout_ms)
{
    flush_input(p);
    int rc = send_line(p, cmd);
    if (rc) return rc;
    return read_line(p, out, n, timeout_ms);
}

int psu_write(psu_t *p, const char *cmd, bool check)
{
    int rc = send_line(p, cmd);
    if (rc) return rc;
    p->state_n = 0;                    /* estado em cache pode ter mudado */
    return check ? psu_check_error(p) : PSU_OK;
}

static int writef(psu_t *p, bool check, const char *fmt, ...)
{
    char cmd[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    return psu_write(p, cmd, check);
}

int psu_read_error(psu_t *p, int *code, char *msg, size_t n, uint32_t timeout_ms)
{
    char r[128];
    int rc = psu_query(p, "SYST:ERR?", r, sizeof r, timeout_ms);
    if (rc) return rc;
    char *end;
    long c = strtol(r, &end, 10);
    if (end == r || (*end && *end != ',')) return link_err(p, "resposta invalida: '%s'", r);
    const char *m = *end ? end + 1 : "";
    while (*m == ' ' || *m == '"') m++;
    size_t len = strlen(m);
    while (len && (m[len - 1] == '"' || m[len - 1] == ' ')) len--;
    if (len >= n) len = n - 1;
    memcpy(msg, m, len);
    msg[len] = '\0';
    *code = (int)c;
    return PSU_OK;
}

int psu_errors(psu_t *p, void (*cb)(void *, int, const char *), void *arg, uint32_t timeout_ms)
{
    for (int k = 0; k < 20; k++) {
        int code;
        char msg[96];
        int rc = psu_read_error(p, &code, msg, sizeof msg, timeout_ms);
        if (rc) return rc;
        if (code == 0) break;
        if (cb) cb(arg, code, msg);
    }
    return PSU_OK;
}

static void first_error(void *arg, int code, const char *msg)
{
    psu_t *p = arg;
    if (!p->scpi_code) {
        p->scpi_code = code;
        snprintf(p->scpi_msg, sizeof p->scpi_msg, "%s", msg);
    }
}

int psu_check_error(psu_t *p)
{
    p->scpi_code = 0;
    p->scpi_msg[0] = '\0';
    int rc = psu_errors(p, first_error, p, 0);
    if (rc) return rc;
    return p->scpi_code ? PSU_ESCPI : PSU_OK;
}

void psu_device_clear(psu_t *p)
{
    trace(p, '>', "<Ctrl-C>");
    p->io.write(p->io.ctx, (const uint8_t *)"\x03", 1);
    if (p->io.activity) p->io.activity(p->io.ctx, false);
    p->last_tx_ms = now(p);
    p->has_last_tx = true;
    nap(p, 150);              /* ja cobre o min_gap_ms com folga */
    flush_input(p);
}

/* ------------------------------------------------------------------ sessao */

int psu_connect(psu_t *p, bool lock_panel, bool labtest)
{
    int rc;
    const char *rem = lock_panel ? "SYST:RWL" : "SYST:REM";
    p->paused = false;
    flush_input(p);
    nap(p, 300);                       /* porta/fonte estabilizam */
    psu_device_clear(p);               /* desfaz travamento DTR/DSR de sessoes anteriores */
    if (labtest) {
        /* sequencia do Lab Test (validada na fonte real): *CLS, esvazia SYST:ERR?,
         * *IDN?; so depois SYST:REM */
        if ((rc = psu_write(p, "*CLS", false))) return rc;
        if ((rc = psu_errors(p, NULL, NULL, 1200))) return rc;
        if ((rc = psu_query(p, "*IDN?", p->idn, sizeof p->idn, 1200))) return rc;
        if ((rc = psu_write(p, rem, false))) return rc;
        nap(p, 100);
    } else {
        if ((rc = psu_write(p, rem, false))) return rc;
        nap(p, 100);
        if ((rc = psu_write(p, "*CLS", false))) return rc;
        if ((rc = psu_errors(p, NULL, NULL, 1200))) return rc;   /* fila vazia: sem beep */
        if ((rc = psu_query(p, "*IDN?", p->idn, sizeof p->idn, 1200))) return rc;
    }
    p->state_n = 0;                    /* forca releitura do estado */
    return PSU_OK;
}

int psu_ping(psu_t *p, char *out, size_t n) { return psu_query(p, "*IDN?", out, n, 0); }
int psu_remote(psu_t *p, bool lock) { return psu_write(p, lock ? "SYST:RWL" : "SYST:REM", false); }
int psu_local(psu_t *p) { return psu_write(p, "SYST:LOC", false); }

/* ---------------------------------------------------------------- leitura */

static int q_num(psu_t *p, const char *cmd, double *v)
{
    char r[64];
    int rc = psu_query(p, cmd, r, sizeof r, 0);
    if (rc) return rc;
    char *end;
    *v = strtod(r, &end);
    if (end == r) return link_err(p, "resposta invalida a %s: '%s'", cmd, r);
    return PSU_OK;
}

static bool to_bool(const char *s)
{
    while (*s == ' ') s++;
    return !strcmp(s, "1") || ieq(s, "ON");
}

static int q_bool(psu_t *p, const char *cmd, bool *b)
{
    char r[32];
    int rc = psu_query(p, cmd, r, sizeof r, 0);
    if (rc) return rc;
    *b = to_bool(r);
    return PSU_OK;
}

static int q_word(psu_t *p, const char *cmd, char *out, size_t n)
{
    int rc = psu_query(p, cmd, out, n, 0);
    if (rc) return rc;
    for (char *s = out; *s; s++) *s = (char)toupper((unsigned char)*s);
    return PSU_OK;
}

int psu_measure(psu_t *p, psu_meas_t *m)
{
    int rc;
    if (p->compound) {
        char r[160];
        rc = psu_query(p, "MEAS:VOLT:DC?;:MEAS:CURR:DC?;:OUTP?;:VOLT:PROT:TRIP?;:CURR:PROT:TRIP?",
                       r, sizeof r, 0);
        if (rc) return rc;
        char *f[5];
        int k = 0;
        char *save = r;
        for (char *t = r; k < 5; t++) {
            if (*t == ';' || *t == '\0') {
                bool last = (*t == '\0');
                *t = '\0';
                f[k++] = save;
                save = t + 1;
                if (last) break;
            }
        }
        if (k != 5) return link_err(p, "resposta composta invalida");
        m->v = strtod(f[0], NULL);
        m->i = strtod(f[1], NULL);
        m->out = to_bool(f[2]);
        m->ovp_t = to_bool(f[3]);
        m->ocp_t = to_bool(f[4]);
        return PSU_OK;
    }
    if ((rc = q_num(p, "MEAS:VOLT:DC?", &m->v))) return rc;
    nap(p, 50);
    if ((rc = q_num(p, "MEAS:CURR:DC?", &m->i))) return rc;
    /* saida/protecoes mudam raramente: reler a cada state_every leituras */
    if (p->state_n <= 0) {
        if ((rc = q_bool(p, "OUTP?", &p->st_out))) return rc;
        if ((rc = q_bool(p, "VOLT:PROT:TRIP?", &p->st_ovp_t))) return rc;
        if ((rc = q_bool(p, "CURR:PROT:TRIP?", &p->st_ocp_t))) return rc;
        p->state_n = p->state_every;
    }
    p->state_n--;
    m->out = p->st_out;
    m->ovp_t = p->st_ovp_t;
    m->ocp_t = p->st_ocp_t;
    return PSU_OK;
}

int psu_status(psu_t *p, psu_status_t *s)
{
    int rc;
    if ((rc = q_word(p, "VOLT:RANG?", s->range, sizeof s->range))) return rc;
    if ((rc = q_num(p, "VOLT?", &s->vset))) return rc;
    if ((rc = q_num(p, "CURR?", &s->iset))) return rc;
    if ((rc = q_bool(p, "OUTP?", &s->out))) return rc;
    if ((rc = q_num(p, "VOLT:PROT?", &s->ovp))) return rc;
    if ((rc = q_bool(p, "VOLT:PROT:STAT?", &s->ovp_en))) return rc;
    if ((rc = q_num(p, "CURR:PROT?", &s->ocp))) return rc;
    if ((rc = q_bool(p, "CURR:PROT:STAT?", &s->ocp_en))) return rc;
    if ((rc = q_bool(p, "DISP?", &s->disp))) return rc;
    if ((rc = q_word(p, "TRIG:SOUR?", s->trig_src, sizeof s->trig_src))) return rc;
    if ((rc = q_num(p, "TRIG:DEL?", &s->trig_del))) return rc;
    if ((rc = q_num(p, "VOLT:TRIG?", &s->vtrig))) return rc;
    if ((rc = q_num(p, "CURR:TRIG?", &s->itrig))) return rc;
    return PSU_OK;
}

/* ---------------------------------------------------------------- comandos */

const char *psu_norm_range(const char *r)
{
    if (ieq(r, "P25V") || ieq(r, "LOW") || ieq(r, "P25")) return "P25V";
    if (ieq(r, "P50V") || ieq(r, "HIGH") || ieq(r, "P50")) return "P50V";
    return NULL;
}

void psu_range_limits(const char *rng, double *vmax, double *imax)
{
    const char *r = rng ? psu_norm_range(rng) : NULL;
    if (r && !strcmp(r, "P25V")) {
        *vmax = PSU_P25V_VMAX;
        *imax = PSU_P25V_IMAX;
    } else if (r) {
        *vmax = PSU_P50V_VMAX;
        *imax = PSU_P50V_IMAX;
    } else {                  /* faixa desconhecida: limites mais largos; a fonte valida */
        *vmax = PSU_P50V_VMAX;
        *imax = PSU_P25V_IMAX;
    }
}

int psu_output(psu_t *p, bool on) { return psu_write(p, on ? "OUTP ON" : "OUTP OFF", true); }

int psu_set_range(psu_t *p, const char *rng)
{
    const char *r = psu_norm_range(rng);
    if (!r) return arg_err(p, "faixa invalida");
    return writef(p, true, "VOLT:RANG %s", r);
}

int psu_set_voltage(psu_t *p, double v) { return writef(p, true, "VOLT %.4f", v); }
int psu_set_current(psu_t *p, double a) { return writef(p, true, "CURR %.4f", a); }
int psu_apply(psu_t *p, double v, double a) { return writef(p, true, "APPL %.4f,%.4f", v, a); }

int psu_set_ovp(psu_t *p, const double *level, const bool *en)
{
    int rc;
    if (level && (rc = writef(p, true, "VOLT:PROT %.3f", *level))) return rc;
    if (en && (rc = psu_write(p, *en ? "VOLT:PROT:STAT ON" : "VOLT:PROT:STAT OFF", true))) return rc;
    return PSU_OK;
}

int psu_set_ocp(psu_t *p, const double *level, const bool *en)
{
    int rc;
    if (level && (rc = writef(p, true, "CURR:PROT %.4f", *level))) return rc;
    if (en && (rc = psu_write(p, *en ? "CURR:PROT:STAT ON" : "CURR:PROT:STAT OFF", true))) return rc;
    return PSU_OK;
}

int psu_clear_protection(psu_t *p)
{
    /* a causa (sobretensao/sobrecorrente) deve ter sido removida antes */
    int rc = psu_write(p, "VOLT:PROT:CLE", true);
    return rc ? rc : psu_write(p, "CURR:PROT:CLE", true);
}

int psu_save(psu_t *p, int n)
{
    if (n < 1 || n > PSU_MEM_SLOTS) return arg_err(p, "memoria 1..5");
    return writef(p, true, "*SAV %d", n);
}

int psu_recall(psu_t *p, int n)
{
    if (n < 1 || n > PSU_MEM_SLOTS) return arg_err(p, "memoria 1..5");
    return writef(p, true, "*RCL %d", n);
}

int psu_display(psu_t *p, bool on) { return psu_write(p, on ? "DISP ON" : "DISP OFF", true); }

int psu_display_text(psu_t *p, const char *text)
{
    char t[13];
    size_t k = 0;
    for (; *text && k < 12; text++) {
        unsigned char c = (unsigned char)*text;
        t[k++] = (c == '"') ? '\'' : (c < 0x20 || c >= 0x7f) ? ' ' : (char)c;
    }
    t[k] = '\0';
    return writef(p, true, "DISP:TEXT \"%s\"", t);
}

int psu_display_text_clear(psu_t *p) { return psu_write(p, "DISP:TEXT:CLE", true); }
int psu_beep(psu_t *p) { return psu_write(p, "SYST:BEEP", true); }
int psu_reset(psu_t *p) { return psu_write(p, "*RST", true); }

int psu_trigger_source(psu_t *p, const char *src)
{
    if (ieq(src, "BUS")) return psu_write(p, "TRIG:SOUR BUS", true);
    if (ieq(src, "IMM")) return psu_write(p, "TRIG:SOUR IMM", true);
    return arg_err(p, "fonte de trigger BUS|IMM");
}

int psu_trigger_delay(psu_t *p, double s) { return writef(p, true, "TRIG:DEL %d", (int)s); }

int psu_trigger_levels(psu_t *p, const double *v, const double *a)
{
    int rc;
    if (v && (rc = writef(p, true, "VOLT:TRIG %.4f", *v))) return rc;
    if (a && (rc = writef(p, true, "CURR:TRIG %.4f", *a))) return rc;
    return PSU_OK;
}

int psu_initiate(psu_t *p) { return psu_write(p, "INIT", true); }
int psu_trigger(psu_t *p) { return psu_write(p, "*TRG", true); }
