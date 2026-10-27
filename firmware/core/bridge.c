#include "bridge.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "json.h"

#define LINE_MAX 768

enum { DOK = 0, DCMD = 1 };    /* + codigos PSU_E* (negativos) */

typedef enum { R_NONE, R_STR, R_INT, R_BOOL, R_RAW } res_kind_t;

typedef struct {
    res_kind_t kind;
    char s[384];
    long i;
    bool b;
} res_t;

static uint32_t now(bridge_t *b) { return b->psu->io.now_ms(b->psu->io.ctx); }

/* ------------------------------------------------------------------ saida */

static void emit(bridge_t *b, jw_t *w)
{
    if (jw_end(w)) b->send(b->send_ctx, w->buf);
}

static void set_link(bridge_t *b, bool ok, const char *reason)
{
    if (ok == b->link) return;
    b->link = ok;
    char buf[LINE_MAX];
    jw_t w;
    jw_begin(&w, buf, sizeof buf);
    jw_str(&w, "t", "link");
    jw_bool(&w, "ok", ok);
    if (ok)
        jw_str(&w, "idn", b->psu->idn);
    else if (reason && *reason)
        jw_str(&w, "err", reason);
    emit(b, &w);
}

static int send_measure(bridge_t *b)
{
    psu_meas_t m;
    int rc = psu_measure(b->psu, &m);
    if (rc) return rc;
    b->errs = 0;
    char buf[192];
    jw_t w;
    jw_begin(&w, buf, sizeof buf);
    jw_str(&w, "t", "m");
    jw_num(&w, "v", m.v);
    jw_num(&w, "i", m.i);
    jw_bool(&w, "o", m.out);
    jw_bool(&w, "ovp_t", m.ovp_t);
    jw_bool(&w, "ocp_t", m.ocp_t);
    emit(b, &w);
    return PSU_OK;
}

static int send_status(bridge_t *b)
{
    psu_status_t s;
    int rc = psu_status(b->psu, &s);
    if (rc) return rc;
    b->state = s;
    b->has_state = true;
    char buf[LINE_MAX];
    jw_t w;
    jw_begin(&w, buf, sizeof buf);
    jw_str(&w, "t", "st");
    jw_str(&w, "range", s.range);
    jw_num(&w, "vset", s.vset);
    jw_num(&w, "iset", s.iset);
    jw_bool(&w, "out", s.out);
    jw_num(&w, "ovp", s.ovp);
    jw_bool(&w, "ovp_en", s.ovp_en);
    jw_num(&w, "ocp", s.ocp);
    jw_bool(&w, "ocp_en", s.ocp_en);
    jw_bool(&w, "disp", s.disp);
    jw_str(&w, "trig_src", s.trig_src);
    jw_num(&w, "trig_del", s.trig_del);
    jw_num(&w, "vtrig", s.vtrig);
    jw_num(&w, "itrig", s.itrig);
    jw_int(&w, "poll", (long)b->poll_ms);
    emit(b, &w);
    return PSU_OK;
}

static void emit_scpi_event(bridge_t *b)
{
    char buf[256];
    jw_t w;
    jw_begin(&w, buf, sizeof buf);
    jw_str(&w, "t", "e");
    jw_int(&w, "code", b->psu->scpi_code);
    jw_str(&w, "msg", b->psu->scpi_msg);
    emit(b, &w);
}

/* ---------------------------------------------------------------- estado */

void bridge_init(bridge_t *b, psu_t *psu, void (*send)(void *, const char *), void *ctx)
{
    memset(b, 0, sizeof *b);
    b->psu = psu;
    b->send = send;
    b->send_ctx = ctx;
    b->poll_ms = POLL_MS;
    b->poll_min_ms = POLL_MIN_MS;
    b->reconnect_ms = RECONNECT_MS;
    b->lock_panel = PSU_LOCK_FRONT_PANEL;
    b->labtest = PSU_LABTEST_SEQUENCE;
}

void bridge_on_client(bridge_t *b, bool connected)
{
    b->client = connected;
    if (!connected) return;
    b->t_link_set = false;           /* tenta (re)ligar a fonte imediatamente */
    char buf[LINE_MAX];
    jw_t w;
    jw_begin(&w, buf, sizeof buf);
    jw_str(&w, "t", "hello");
    jw_str(&w, "fw", FW_VERSION);
    jw_bool(&w, "link", b->link);
    jw_int(&w, "poll", (long)b->poll_ms);
    if (b->link) jw_str(&w, "idn", b->psu->idn);
    emit(b, &w);
    b->need_status = b->link;
}

/* recuo progressivo (3 s, 6 s, 12 s ... ate 30 s): cada tentativa com a serial
 * errada pode fazer a fonte apitar, entao nao insistir a cada 3 s */
static uint32_t retry_ms(bridge_t *b)
{
    int sh = b->fails < 4 ? b->fails : 4;
    uint32_t cap = b->reconnect_ms > 30000 ? b->reconnect_ms : 30000;
    uint32_t t = b->reconnect_ms << sh;
    return t < cap ? t : cap;
}

/* falha transitoria nao derruba o link: so 3 seguidas marcam erro */
static void link_error(bridge_t *b, const char *msg)
{
    if (++b->errs < 3) return;
    b->errs = 0;
    b->fails = 0;
    snprintf(b->last_err, sizeof b->last_err, "%s", msg);
    set_link(b, false, msg);
    b->t_link = now(b);
    b->t_link_set = true;
}

bool bridge_try_link(bridge_t *b)
{
    int rc = psu_connect(b->psu, b->lock_panel, b->labtest);
    if (rc) {
        b->fails++;
        snprintf(b->last_err, sizeof b->last_err, "%s",
                 rc == PSU_ESCPI ? b->psu->scpi_msg : b->psu->err);
        set_link(b, false, b->last_err);
        return false;
    }
    b->fails = 0;
    b->errs = 0;
    b->link = false;                 /* sempre avisa o app de um link novo */
    set_link(b, true, NULL);
    b->need_status = true;
    return true;
}

void bridge_tick(bridge_t *b)
{
    uint32_t t = now(b);
    if (!b->link) {
        if (!b->t_link_set || t - b->t_link >= retry_ms(b)) {
            b->t_link = t;
            b->t_link_set = true;
            bridge_try_link(b);
        }
        return;
    }
    if (!b->client) return;
    int rc = PSU_OK;
    if (b->need_status) {
        b->need_status = false;
        rc = send_status(b);
    }
    if (!rc && b->poll_ms && t - b->t_poll >= b->poll_ms) {
        b->t_poll = t;
        rc = send_measure(b);
    }
    if (rc == PSU_ELINK)
        link_error(b, b->psu->err);
    else if (rc == PSU_ESCPI)
        emit_scpi_event(b);
}

/* -------------------------------------------------------------- comandos */

static bool get_num(const json_obj_t *d, const char *k, double lo, double hi, bool lim,
                    double *x, char *err, size_t n)
{
    const json_kv_t *kv = json_get(d, k);
    if (!kv) {
        snprintf(err, n, "campo '%s' ausente", k);
        return false;
    }
    if (!json_number(kv, x)) {
        snprintf(err, n, "campo '%s' invalido", k);
        return false;
    }
    if (lim && (*x < lo || *x > hi)) {
        snprintf(err, n, "%s fora da faixa (%g..%g)", k, lo, hi);
        return false;
    }
    return true;
}

#define NUM(k, lo, hi, x) \
    do { if (!get_num(d, k, lo, hi, true, &(x), err, errn)) return DCMD; } while (0)

static const char *get_str(const json_obj_t *d, const char *k, char *tmp, size_t n)
{
    const json_kv_t *kv = json_get(d, k);
    if (!kv || kv->type == JT_NULL) return "";
    if (kv->type == JT_STR) return kv->str;
    size_t len = kv->raw_len < n - 1 ? kv->raw_len : n - 1;   /* str() de numero/bool */
    memcpy(tmp, kv->raw, len);
    tmp[len] = '\0';
    return tmp;
}

typedef struct {
    char *buf;
    size_t cap, len;
    bool first;
} errlist_t;

static void collect_err(void *arg, int code, const char *msg)
{
    errlist_t *e = arg;
    char item[128], esc[160];
    snprintf(item, sizeof item, "%d,%s", code, msg);
    size_t n = json_escape(esc, sizeof esc, item);
    if (e->len + n + 3 >= e->cap) return;
    if (!e->first) e->buf[e->len++] = ',';
    e->first = false;
    memcpy(e->buf + e->len, esc, n);
    e->len += n;
    e->buf[e->len] = '\0';
}

static int dispatch(bridge_t *b, const char *c, const json_obj_t *d, res_t *res,
                    char *err, size_t errn)
{
    psu_t *p = b->psu;
    double x, y;
    char tmp[64];

    /* comandos locais (nao precisam da fonte) */
    if (!strcmp(c, "poll")) {
        NUM("ms", 0, 60000, x);
        long ms = (long)x;
        b->poll_ms = ms == 0 ? 0 : (uint32_t)(ms > (long)b->poll_min_ms ? ms : (long)b->poll_min_ms);
        res->kind = R_INT;
        res->i = (long)b->poll_ms;
        return DOK;
    }
    if (!strcmp(c, "ping")) {
        res->kind = R_STR;
        strcpy(res->s, "pong");
        return DOK;
    }
    if (!strcmp(c, "version")) {
        res->kind = R_STR;
        strcpy(res->s, FW_VERSION);
        return DOK;
    }
    if (!strcmp(c, "link")) {
        if (!bridge_try_link(b)) {
            snprintf(err, errn, "sem resposta da fonte: %s", b->last_err);
            return DCMD;
        }
        res->kind = R_BOOL;
        res->b = true;
        return DOK;
    }

    if (!b->link) {
        snprintf(err, errn, "fonte desconectada");
        return DCMD;
    }
    double vmax, imax;
    psu_range_limits(b->has_state ? b->state.range : NULL, &vmax, &imax);
    bool changed = true;
    int rc = PSU_OK;

    if (!strcmp(c, "out")) {
        rc = psu_output(p, json_truthy(d, "on"));
    } else if (!strcmp(c, "volt")) {
        NUM("v", 0, vmax, x);
        rc = psu_set_voltage(p, x);
    } else if (!strcmp(c, "curr")) {
        NUM("a", 0, imax, x);
        rc = psu_set_current(p, x);
    } else if (!strcmp(c, "apply")) {
        NUM("v", 0, vmax, x);
        NUM("a", 0, imax, y);
        rc = psu_apply(p, x, y);
    } else if (!strcmp(c, "range")) {
        rc = psu_set_range(p, get_str(d, "r", tmp, sizeof tmp));
    } else if (!strcmp(c, "ovp") || !strcmp(c, "ocp")) {
        bool ovp = c[1] == 'v';
        const char *k = ovp ? "v" : "a";
        bool en, has_en = json_has(d, "on");
        if (json_has(d, k)) {
            if (ovp)
                NUM(k, PSU_OVP_MIN, PSU_OVP_MAX, x);
            else
                NUM(k, PSU_OCP_MIN, PSU_OCP_MAX, x);
        }
        en = json_truthy(d, "on");
        const double *lvl = json_has(d, k) ? &x : NULL;
        const bool *pen = has_en ? &en : NULL;
        rc = ovp ? psu_set_ovp(p, lvl, pen) : psu_set_ocp(p, lvl, pen);
    } else if (!strcmp(c, "prot_clear")) {
        rc = psu_clear_protection(p);
    } else if (!strcmp(c, "save")) {
        NUM("n", 1, 5, x);
        rc = psu_save(p, (int)x);
        changed = false;
    } else if (!strcmp(c, "recall")) {
        NUM("n", 1, 5, x);
        rc = psu_recall(p, (int)x);
    } else if (!strcmp(c, "disp")) {
        rc = psu_display(p, json_truthy(d, "on"));
    } else if (!strcmp(c, "text")) {
        rc = psu_display_text(p, get_str(d, "s", tmp, sizeof tmp));
        changed = false;
    } else if (!strcmp(c, "text_clear")) {
        rc = psu_display_text_clear(p);
        changed = false;
    } else if (!strcmp(c, "beep")) {
        rc = psu_beep(p);
        changed = false;
    } else if (!strcmp(c, "reset")) {
        rc = psu_reset(p);
    } else if (!strcmp(c, "local")) {
        rc = psu_local(p);
        changed = false;
    } else if (!strcmp(c, "remote")) {
        rc = psu_remote(p, json_has(d, "lock") ? json_truthy(d, "lock") : b->lock_panel);
        changed = false;
    } else if (!strcmp(c, "trig_src")) {
        rc = psu_trigger_source(p, get_str(d, "s", tmp, sizeof tmp));
    } else if (!strcmp(c, "trig_del")) {
        NUM("s", 0, 3600, x);
        rc = psu_trigger_delay(p, x);
    } else if (!strcmp(c, "trig_lvl")) {
        bool hv = json_has(d, "v"), ha = json_has(d, "a");
        if (hv) NUM("v", 0, vmax, x);
        if (ha) NUM("a", 0, imax, y);
        rc = psu_trigger_levels(p, hv ? &x : NULL, ha ? &y : NULL);
    } else if (!strcmp(c, "init")) {
        rc = psu_initiate(p);
        changed = false;
    } else if (!strcmp(c, "trg")) {
        rc = psu_trigger(p);
    } else if (!strcmp(c, "status")) {
        rc = send_status(b);
        changed = false;
    } else if (!strcmp(c, "meas")) {
        rc = send_measure(b);
        changed = false;
    } else if (!strcmp(c, "err")) {
        errlist_t e = {res->s, sizeof res->s, 0, true};
        res->s[e.len++] = '[';
        res->s[e.len] = '\0';
        rc = psu_errors(p, collect_err, &e, 0);
        res->s[e.len++] = ']';
        res->s[e.len] = '\0';
        res->kind = R_RAW;
        changed = false;
    } else if (!strcmp(c, "idn")) {
        rc = psu_ping(p, res->s, sizeof res->s);
        res->kind = R_STR;
        changed = false;
    } else if (!strcmp(c, "scpi")) {
        const char *q0 = get_str(d, "q", tmp, sizeof tmp);
        char q[160];
        while (*q0 == ' ' || *q0 == '\t') q0++;
        snprintf(q, sizeof q, "%s", q0);
        size_t n = strlen(q);
        while (n && (q[n - 1] == ' ' || q[n - 1] == '\t' || q[n - 1] == '\r' || q[n - 1] == '\n'))
            q[--n] = '\0';
        if (!n) {
            snprintf(err, errn, "SCPI vazio");
            return DCMD;
        }
        if (strchr(q, '?')) {
            rc = psu_query(p, q, res->s, sizeof res->s, 0);
            res->kind = R_STR;
        } else {
            rc = psu_write(p, q, true);
        }
        /* comando livre pode ter alterado qualquer coisa */
    } else {
        snprintf(err, errn, "comando desconhecido: %s", c);
        return DCMD;
    }

    if (rc == PSU_EARG) {
        snprintf(err, errn, "%s", p->err);
        return DCMD;
    }
    if (rc) {
        res->kind = R_NONE;
        return rc;
    }
    if (changed) b->need_status = true;
    return DOK;
}

void bridge_handle_line(bridge_t *b, const char *line)
{
    static json_obj_t d;      /* grande demais para a pilha; so o loop principal usa */
    static res_t res;
    char text[LINE_MAX];
    char err[160];
    char buf[LINE_MAX];
    jw_t w;

    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') line++;
    snprintf(text, sizeof text, "%s", line);
    size_t n = strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n'))
        text[--n] = '\0';
    if (!n) return;

    memset(&res, 0, sizeof res);
    err[0] = '\0';
    const json_kv_t *id = NULL;
    const char *cmd = NULL;
    int rc;
    if (!json_parse(&d, text)) {
        d.n = 0;
        snprintf(err, sizeof err, "JSON invalido");
        rc = DCMD;
    } else {
        id = json_get(&d, "id");
        const json_kv_t *c = json_get(&d, "cmd");
        if (!c || c->type != JT_STR) {
            snprintf(err, sizeof err, "faltando 'cmd'");
            rc = DCMD;
        } else {
            cmd = c->str;
            rc = dispatch(b, cmd, &d, &res, err, sizeof err);
        }
    }

    jw_begin(&w, buf, sizeof buf);
    jw_str(&w, "t", "r");
    if (rc == DOK) {
        jw_bool(&w, "ok", true);
        jw_str(&w, "cmd", cmd);
        switch (res.kind) {
        case R_STR: jw_str(&w, "d", res.s); break;
        case R_INT: jw_int(&w, "d", res.i); break;
        case R_BOOL: jw_bool(&w, "d", res.b); break;
        case R_RAW: jw_raw(&w, "d", res.s, strlen(res.s)); break;
        default: break;
        }
    } else if (rc == DCMD) {
        jw_bool(&w, "ok", false);
        jw_str(&w, "err", err);
    } else if (rc == PSU_ESCPI) {
        char m[160];
        snprintf(m, sizeof m, "SCPI %d: %s", b->psu->scpi_code, b->psu->scpi_msg);
        jw_bool(&w, "ok", false);
        jw_str(&w, "err", m);
        jw_int(&w, "code", b->psu->scpi_code);
        b->need_status = true;
    } else {                              /* PSU_ELINK */
        link_error(b, b->psu->err);
        jw_bool(&w, "ok", false);
        jw_str(&w, "err", "sem resposta da fonte");
    }
    if (id) jw_raw(&w, "id", id->raw, id->raw_len);
    emit(b, &w);
}
