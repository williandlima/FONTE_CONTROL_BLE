#include "sim_e3634a.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *err_msg(int code)
{
    switch (code) {
    case -113: return "Undefined header";
    case -221: return "Settings conflict";
    case -222: return "Data out of range";
    case -224: return "Illegal parameter value";
    case -410: return "Query INTERRUPTED";
    default: return "Error";
    }
}

static void push_err(sim_t *s, int code)
{
    if (s->errn < (int)(sizeof s->errq / sizeof s->errq[0])) s->errq[s->errn++] = code;
    s->beeps++;                    /* a fonte apita a cada erro */
}

void sim_reset_state(sim_t *s)
{
    strcpy(s->range, "P25V");
    s->vset = 0.0;
    s->iset = 7.0;
    s->out_on = false;
    s->ovp = 55.0;
    s->ovp_en = s->ovp_trip = false;
    s->ocp = 7.5;
    s->ocp_en = s->ocp_trip = false;
    s->disp = true;
    strcpy(s->trig_src, "IMM");
    s->trig_del = 0;
    s->vtrig = 0;
    s->itrig = 7.0;
    s->errn = 0;
}

void sim_init(sim_t *s)
{
    memset(s, 0, sizeof *s);
    s->wiring = WIRE_DSR_PULLUP;
    s->alive = true;
    s->compound_ok = true;
    s->load_ohm = 10.0;
    s->dtr = true;
    sim_reset_state(s);
}

bool sim_dsr(const sim_t *s)
{
    switch (s->wiring) {
    case WIRE_DSR_PULLUP: return true;
    case WIRE_JUMPER_4_6: return s->dtr;
    default: return false;
    }
}

static void vmax_imax(const sim_t *s, double *v, double *i)
{
    if (!strcmp(s->range, "P50V")) {
        *v = 51.5;
        *i = 4.12;
    } else {
        *v = 25.75;
        *i = 7.21;
    }
}

static void vi(sim_t *s, double *v, double *i)
{
    *v = *i = 0;
    if (!s->out_on || s->ovp_trip || s->ocp_trip) return;
    double cur = s->vset / s->load_ohm, volt = s->vset;
    if (cur > s->iset) {
        cur = s->iset;
        volt = cur * s->load_ohm;
    }
    if (s->ovp_en && volt > s->ovp) {
        s->ovp_trip = true;
        s->out_on = false;
        return;
    }
    if (s->ocp_en && cur > s->ocp) {
        s->ocp_trip = true;
        s->out_on = false;
        return;
    }
    *v = volt;
    *i = cur;
}

static int onoff(const char *a, bool *b)
{
    if (!strcmp(a, "ON") || !strcmp(a, "1")) { *b = true; return 0; }
    if (!strcmp(a, "OFF") || !strcmp(a, "0")) { *b = false; return 0; }
    return -1;
}

static int num(const char *a, double *x)
{
    char *end;
    *x = strtod(a, &end);
    return (end == a) ? -1 : 0;
}

#define FE(x) snprintf(ans, cap, "%+.8E", (double)(x))
#define B01(x) snprintf(ans, cap, "%s", (x) ? "1" : "0")

/* Executa um comando. Retorna 1 se gerou resposta em ans, 0 se nao, -1 se erro. */
static int one(sim_t *s, const char *h, char *arg, char *ans, size_t cap)
{
    double vmax, imax, x, y;
    bool b;
    vmax_imax(s, &vmax, &imax);
    if (strcmp(h, "DISP:TEXT"))           /* o texto do display mantem a caixa */
        for (char *t = arg; *t; t++) *t = (char)toupper((unsigned char)*t);

    if (!strcmp(h, "*IDN?")) { snprintf(ans, cap, "%s", SIM_IDN); return 1; }
    if (!strcmp(h, "*RST")) { sim_reset_state(s); return 0; }
    if (!strcmp(h, "*CLS")) { s->errn = 0; return 0; }
    if (!strcmp(h, "*OPC?")) { snprintf(ans, cap, "1"); return 1; }
    if (!strcmp(h, "SYST:REM") || !strcmp(h, "SYST:RWL")) { s->remote = true; return 0; }
    if (!strcmp(h, "SYST:LOC")) { s->remote = false; return 0; }
    if (!strcmp(h, "SYST:ERR?")) {
        if (s->errn) {
            int c = s->errq[0];
            memmove(s->errq, s->errq + 1, (size_t)(s->errn - 1) * sizeof s->errq[0]);
            s->errn--;
            snprintf(ans, cap, "%+d,\"%s\"", c, err_msg(c));
        } else {
            snprintf(ans, cap, "+0,\"No error\"");
        }
        return 1;
    }
    if (!s->remote) { push_err(s, -221); return -1; }   /* RS-232 sem SYST:REM */
    if (!strcmp(h, "SYST:VERS?")) { snprintf(ans, cap, "1994.0"); return 1; }
    if (!strcmp(h, "SYST:BEEP")) { s->beeps++; return 0; }
    if (!strcmp(h, "*SAV") || !strcmp(h, "*RCL")) {
        int n = atoi(arg);
        if (n < 1 || n > 5) { push_err(s, -222); return -1; }
        if (h[1] == 'S') {
            s->mem[n].used = true;
            strcpy(s->mem[n].range, s->range);
            s->mem[n].vset = s->vset; s->mem[n].iset = s->iset;
            s->mem[n].ovp = s->ovp; s->mem[n].ocp = s->ocp;
            s->mem[n].ovp_en = s->ovp_en; s->mem[n].ocp_en = s->ocp_en;
        } else if (s->mem[n].used) {
            strcpy(s->range, s->mem[n].range);
            s->vset = s->mem[n].vset; s->iset = s->mem[n].iset;
            s->ovp = s->mem[n].ovp; s->ocp = s->mem[n].ocp;
            s->ovp_en = s->mem[n].ovp_en; s->ocp_en = s->mem[n].ocp_en;
            s->out_on = false;
        } else {
            sim_reset_state(s);
        }
        return 0;
    }
    if (!strcmp(h, "*TRG")) {
        if (!strcmp(s->trig_src, "BUS")) { s->vset = s->vtrig; s->iset = s->itrig; }
        return 0;
    }
    if (!strcmp(h, "VOLT") || !strcmp(h, "VOLT:LEV")) {
        if (num(arg, &x) || x < 0 || x > vmax) { push_err(s, -222); return -1; }
        s->vset = x;
        return 0;
    }
    if (!strcmp(h, "CURR") || !strcmp(h, "CURR:LEV")) {
        if (num(arg, &x) || x < 0 || x > imax) { push_err(s, -222); return -1; }
        s->iset = x;
        return 0;
    }
    if (!strcmp(h, "APPL")) {
        if (sscanf(arg, "%lf,%lf", &x, &y) != 2 || x < 0 || x > vmax || y < 0 || y > imax) {
            push_err(s, -222);
            return -1;
        }
        s->vset = x;
        s->iset = y;
        return 0;
    }
    if (!strcmp(h, "VOLT?")) { FE(s->vset); return 1; }
    if (!strcmp(h, "CURR?")) { FE(s->iset); return 1; }
    if (!strcmp(h, "VOLT:RANG")) {
        const char *r = !strcmp(arg, "LOW") ? "P25V" : !strcmp(arg, "HIGH") ? "P50V" : arg;
        if (strcmp(r, "P25V") && strcmp(r, "P50V")) { push_err(s, -224); return -1; }
        strcpy(s->range, r);
        vmax_imax(s, &vmax, &imax);
        if (s->vset > vmax) s->vset = vmax;
        if (s->iset > imax) s->iset = imax;
        return 0;
    }
    if (!strcmp(h, "VOLT:RANG?")) { snprintf(ans, cap, "%s", s->range); return 1; }
    if (!strcmp(h, "OUTP") || !strcmp(h, "OUTP:STAT")) {
        if (onoff(arg, &b)) { push_err(s, -224); return -1; }
        s->out_on = b;
        return 0;
    }
    if (!strcmp(h, "OUTP?")) { B01(s->out_on); return 1; }
    if (!strcmp(h, "MEAS:VOLT?") || !strcmp(h, "MEAS:VOLT:DC?")) { vi(s, &x, &y); FE(x); return 1; }
    if (!strcmp(h, "MEAS:CURR?") || !strcmp(h, "MEAS:CURR:DC?")) { vi(s, &x, &y); FE(y); return 1; }
    if (!strcmp(h, "VOLT:PROT")) { if (num(arg, &x)) return -1; s->ovp = x; return 0; }
    if (!strcmp(h, "VOLT:PROT?")) { FE(s->ovp); return 1; }
    if (!strcmp(h, "VOLT:PROT:STAT")) { if (onoff(arg, &b)) return -1; s->ovp_en = b; return 0; }
    if (!strcmp(h, "VOLT:PROT:STAT?")) { B01(s->ovp_en); return 1; }
    if (!strcmp(h, "VOLT:PROT:TRIP?")) { B01(s->ovp_trip); return 1; }
    if (!strcmp(h, "VOLT:PROT:CLE")) { s->ovp_trip = false; return 0; }
    if (!strcmp(h, "CURR:PROT")) { if (num(arg, &x)) return -1; s->ocp = x; return 0; }
    if (!strcmp(h, "CURR:PROT?")) { FE(s->ocp); return 1; }
    if (!strcmp(h, "CURR:PROT:STAT")) { if (onoff(arg, &b)) return -1; s->ocp_en = b; return 0; }
    if (!strcmp(h, "CURR:PROT:STAT?")) { B01(s->ocp_en); return 1; }
    if (!strcmp(h, "CURR:PROT:TRIP?")) { B01(s->ocp_trip); return 1; }
    if (!strcmp(h, "CURR:PROT:CLE")) { s->ocp_trip = false; return 0; }
    if (!strcmp(h, "DISP")) { if (onoff(arg, &b)) return -1; s->disp = b; return 0; }
    if (!strcmp(h, "DISP?")) { B01(s->disp); return 1; }
    if (!strcmp(h, "DISP:TEXT")) {
        char *a = arg;
        size_t n = strlen(a);
        if (n >= 2 && a[0] == '"' && a[n - 1] == '"') { a[n - 1] = '\0'; a++; }
        snprintf(s->text, sizeof s->text, "%s", a);
        return 0;
    }
    if (!strcmp(h, "DISP:TEXT:CLE")) { s->text[0] = '\0'; return 0; }
    if (!strcmp(h, "TRIG:SOUR")) { snprintf(s->trig_src, sizeof s->trig_src, "%s", arg); return 0; }
    if (!strcmp(h, "TRIG:SOUR?")) { snprintf(ans, cap, "%s", s->trig_src); return 1; }
    if (!strcmp(h, "TRIG:DEL")) { if (num(arg, &x)) return -1; s->trig_del = x; return 0; }
    if (!strcmp(h, "TRIG:DEL?")) { snprintf(ans, cap, "%+d", (int)s->trig_del); return 1; }
    if (!strcmp(h, "VOLT:TRIG")) { if (num(arg, &x)) return -1; s->vtrig = x; return 0; }
    if (!strcmp(h, "VOLT:TRIG?")) { FE(s->vtrig); return 1; }
    if (!strcmp(h, "CURR:TRIG")) { if (num(arg, &x)) return -1; s->itrig = x; return 0; }
    if (!strcmp(h, "CURR:TRIG?")) { FE(s->itrig); return 1; }
    if (!strcmp(h, "INIT")) {
        if (!strcmp(s->trig_src, "IMM")) { s->vset = s->vtrig; s->iset = s->itrig; }
        return 0;
    }
    push_err(s, -113);
    return -1;
}

static void put_out(sim_t *s, const char *d, size_t n)
{
    if (s->out_len + n > sizeof s->out) n = sizeof s->out - s->out_len;
    memcpy(s->out + s->out_len, d, n);
    s->out_len += n;
}

static void execute(sim_t *s, char *line)
{
    if (s->log_n < (int)(sizeof s->log / sizeof s->log[0])) snprintf(s->log[s->log_n++], sizeof s->log[0], "%s", line);

    /* comando novo com resposta anterior presa (DSR falso) */
    if (s->out_len) {
        s->out_len = 0;
        s->dtr = true;
        push_err(s, -410);
    }

    char resp[512] = "";
    size_t rlen = 0;
    int answers = 0;
    char *save = NULL;
    for (char *part = strtok_r(line, ";", &save); part; part = strtok_r(NULL, ";", &save)) {
        while (*part == ' ' || *part == ':') part++;
        if (!*part) continue;
        char head[48], arg[96] = "";
        char *sp = strchr(part, ' ');
        size_t hl = sp ? (size_t)(sp - part) : strlen(part);
        if (hl >= sizeof head) hl = sizeof head - 1;
        memcpy(head, part, hl);
        head[hl] = '\0';
        for (char *t = head; *t; t++) *t = (char)toupper((unsigned char)*t);
        if (sp) {
            while (*sp == ' ') sp++;
            snprintf(arg, sizeof arg, "%s", sp);
            size_t n = strlen(arg);
            while (n && arg[n - 1] == ' ') arg[--n] = '\0';
        }
        if (head[hl - 1] == '?' && answers && !s->compound_ok) {
            push_err(s, -410);
            continue;
        }
        char ans[128];
        if (one(s, head, arg, ans, sizeof ans) == 1) {
            rlen += (size_t)snprintf(resp + rlen, sizeof resp - rlen, "%s%s", answers ? ";" : "", ans);
            answers++;
        }
    }
    if (!answers) return;
    rlen += (size_t)snprintf(resp + rlen, sizeof resp - rlen, "\r\n");
    s->dtr = false;                      /* quer falar: DTR em FALSO */
    if (s->garbage) {
        static const uint8_t junk[] = {0xf8, 0xfe, 0x80, 0x86, 0xe6, 0x98, '\n'};
        put_out(s, (const char *)junk, sizeof junk);
    } else if (s->flow && rlen > 3) {
        put_out(s, "\x13", 1);
        put_out(s, resp, 3);
        put_out(s, "\x11", 1);
        put_out(s, resp + 3, rlen - 3);
    } else {
        put_out(s, resp, rlen);
    }
}

void sim_write(sim_t *s, const uint8_t *d, size_t n)
{
    if (!s->alive) return;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = d[i];
        if (s->raw_len < sizeof s->raw) s->raw[s->raw_len++] = c;
        if (c == 0x03) {                  /* device clear */
            s->out_len = 0;
            s->in_len = 0;
            s->dtr = true;
            s->cleared++;
            continue;
        }
        if (c == '\n') {
            s->in[s->in_len] = '\0';
            char line[512];
            size_t a = 0, b = s->in_len;
            while (a < b && (s->in[a] == ' ' || s->in[a] == '\r')) a++;
            while (b > a && (s->in[b - 1] == ' ' || s->in[b - 1] == '\r')) b--;
            memcpy(line, s->in + a, b - a);
            line[b - a] = '\0';
            s->in_len = 0;
            if (*line) execute(s, line);
            continue;
        }
        if (s->in_len < sizeof s->in - 1) s->in[s->in_len++] = (char)c;
    }
}

size_t sim_read(sim_t *s, uint8_t *d, size_t max)
{
    if (!s->alive) return 0;
    size_t n = 0;
    /* a fonte confere o DSR antes de cada caractere */
    while (n < max && s->out_len && sim_dsr(s)) {
        d[n++] = s->out[0];
        memmove(s->out, s->out + 1, --s->out_len);
    }
    if (!s->out_len) s->dtr = true;     /* terminou de falar */
    return n;
}

int sim_find_log(const sim_t *s, const char *cmd)
{
    for (int i = 0; i < s->log_n; i++)
        if (!strcmp(s->log[i], cmd)) return i;
    return -1;
}

bool sim_has_error(const sim_t *s, int code)
{
    for (int i = 0; i < s->errn; i++)
        if (s->errq[i] == code) return true;
    return false;
}
