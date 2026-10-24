#include "json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- leitura */

typedef struct {
    const char *p;
    json_obj_t *o;
} jp_t;

static void ws(jp_t *j)
{
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n')
        j->p++;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Le uma string JSON. Se dst != NULL grava o texto decodificado (truncado em cap-1). */
static bool parse_string(jp_t *j, char *dst, size_t cap, size_t *outlen)
{
    size_t n = 0;
    if (*j->p != '"') return false;
    j->p++;
    for (;;) {
        char c = *j->p++;
        if (c == '\0') return false;
        if (c == '"') break;
        if ((unsigned char)c < 0x20) return false;
        if (c == '\\') {
            char e = *j->p++;
            switch (e) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                int v = 0;
                for (int k = 0; k < 4; k++) {
                    int h = hexval(*j->p++);
                    if (h < 0) return false;
                    v = v * 16 + h;
                }
                c = (v < 0x80) ? (char)v : '?';   /* SCPI e ASCII */
                break;
            }
            default:
                return false;
            }
        }
        if (dst && n + 1 < cap) dst[n] = c;
        n++;
    }
    if (dst && cap) dst[n < cap ? n : cap - 1] = '\0';
    if (outlen) *outlen = n;
    return true;
}

static bool skip_value(jp_t *j);

static bool skip_container(jp_t *j, char open, char close)
{
    j->p++;
    ws(j);
    if (*j->p == close) {
        j->p++;
        return true;
    }
    for (;;) {
        ws(j);
        if (open == '{') {
            if (!parse_string(j, NULL, 0, NULL)) return false;
            ws(j);
            if (*j->p++ != ':') return false;
            ws(j);
        }
        if (!skip_value(j)) return false;
        ws(j);
        if (*j->p == ',') {
            j->p++;
            continue;
        }
        if (*j->p == close) {
            j->p++;
            return true;
        }
        return false;
    }
}

static bool parse_number(jp_t *j, double *out)
{
    const char *s = j->p;
    if (*s == '-') s++;
    if (*s < '0' || *s > '9') return false;
    char *end;
    double v = strtod(j->p, &end);
    if (end == j->p) return false;
    j->p = end;
    if (out) *out = v;
    return true;
}

static bool skip_value(jp_t *j)
{
    char c = *j->p;
    if (c == '"') return parse_string(j, NULL, 0, NULL);
    if (c == '{') return skip_container(j, '{', '}');
    if (c == '[') return skip_container(j, '[', ']');
    if (!strncmp(j->p, "true", 4)) { j->p += 4; return true; }
    if (!strncmp(j->p, "false", 5)) { j->p += 5; return true; }
    if (!strncmp(j->p, "null", 4)) { j->p += 4; return true; }
    return parse_number(j, NULL);
}

bool json_parse(json_obj_t *o, const char *text)
{
    jp_t j = {text, o};
    o->n = 0;
    o->pool_used = 0;
    ws(&j);
    if (*j.p != '{') return false;
    j.p++;
    ws(&j);
    if (*j.p == '}') {
        j.p++;
        ws(&j);
        return *j.p == '\0';
    }
    for (;;) {
        char key[JSON_KEY_LEN];
        size_t klen;
        ws(&j);
        if (!parse_string(&j, key, sizeof key, &klen)) return false;
        ws(&j);
        if (*j.p++ != ':') return false;
        ws(&j);

        json_kv_t tmp;
        json_kv_t *kv = (o->n < JSON_MAX_KEYS && klen < JSON_KEY_LEN) ? &o->kv[o->n] : &tmp;
        memset(kv, 0, sizeof *kv);
        strcpy(kv->key, key);
        kv->raw = j.p;
        char c = *j.p;
        if (c == '"') {
            const char *start = j.p;
            size_t len;
            if (!parse_string(&j, NULL, 0, &len)) return false;
            size_t room = JSON_POOL - o->pool_used;
            char *dst = o->pool + o->pool_used;
            const char *save = j.p;
            j.p = start;
            parse_string(&j, room ? dst : NULL, room, NULL);
            j.p = save;
            kv->type = JT_STR;
            kv->str = room ? dst : "";
            o->pool_used += room ? strlen(dst) + 1 : 0;
        } else if (c == '{' || c == '[') {
            if (!skip_value(&j)) return false;
            kv->type = JT_OTHER;
        } else if (!strncmp(j.p, "true", 4)) {
            j.p += 4;
            kv->type = JT_BOOL;
            kv->num = 1;
        } else if (!strncmp(j.p, "false", 5)) {
            j.p += 5;
            kv->type = JT_BOOL;
            kv->num = 0;
        } else if (!strncmp(j.p, "null", 4)) {
            j.p += 4;
            kv->type = JT_NULL;
        } else {
            if (!parse_number(&j, &kv->num)) return false;
            kv->type = JT_NUM;
        }
        kv->raw_len = (size_t)(j.p - kv->raw);
        if (kv != &tmp) {
            /* chave repetida: vale a ultima, como no Python */
            int k;
            for (k = 0; k < o->n; k++)
                if (!strcmp(o->kv[k].key, kv->key)) break;
            if (k < o->n)
                o->kv[k] = *kv;
            else
                o->n++;
        }
        ws(&j);
        if (*j.p == ',') {
            j.p++;
            continue;
        }
        if (*j.p == '}') {
            j.p++;
            break;
        }
        return false;
    }
    ws(&j);
    return *j.p == '\0';
}

const json_kv_t *json_get(const json_obj_t *o, const char *key)
{
    for (int i = 0; i < o->n; i++)
        if (!strcmp(o->kv[i].key, key)) return &o->kv[i];
    return NULL;
}

bool json_has(const json_obj_t *o, const char *key)
{
    return json_get(o, key) != NULL;
}

bool json_truthy(const json_obj_t *o, const char *key)
{
    const json_kv_t *kv = json_get(o, key);
    if (!kv) return false;
    switch (kv->type) {
    case JT_BOOL:
    case JT_NUM: return kv->num != 0;
    case JT_STR: return kv->str[0] != '\0';
    case JT_OTHER: return kv->raw_len > 2;   /* [] e {} sao falsos */
    default: return false;
    }
}

bool json_number(const json_kv_t *kv, double *out)
{
    if (!kv) return false;
    if (kv->type == JT_NUM || kv->type == JT_BOOL) {
        *out = kv->num;
        return isfinite(kv->num);
    }
    if (kv->type == JT_STR) {
        const char *s = kv->str;
        char *end;
        while (*s == ' ') s++;
        if (!*s) return false;
        double v = strtod(s, &end);
        while (*end == ' ') end++;
        if (*end || !isfinite(v)) return false;
        *out = v;
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------- escrita */

static void put(jw_t *w, const char *s, size_t n)
{
    if (w->len + n + 1 > w->cap) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = '\0';
}

size_t json_escape(char *out, size_t cap, const char *s)
{
    size_t n = 0;
#define PUTC(ch) do { if (n + 1 < cap) out[n] = (ch); n++; } while (0)
    PUTC('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            PUTC('\\');
            PUTC((char)c);
        } else if (c == '\n') {
            PUTC('\\'); PUTC('n');
        } else if (c == '\r') {
            PUTC('\\'); PUTC('r');
        } else if (c == '\t') {
            PUTC('\\'); PUTC('t');
        } else if (c < 0x20 || c >= 0x7f) {
            char tmp[8];
            snprintf(tmp, sizeof tmp, "\\u%04x", c);
            for (char *t = tmp; *t; t++) PUTC(*t);
        } else {
            PUTC((char)c);
        }
    }
    PUTC('"');
#undef PUTC
    if (cap) out[n < cap ? n : cap - 1] = '\0';
    return n;
}

static void key(jw_t *w, const char *k)
{
    char tmp[JSON_KEY_LEN + 8];
    if (!w->first) put(w, ",", 1);
    w->first = false;
    size_t n = json_escape(tmp, sizeof tmp, k);
    put(w, tmp, n);
    put(w, ":", 1);
}

void jw_begin(jw_t *w, char *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->first = true;
    w->overflow = false;
    if (cap) buf[0] = '\0';
    put(w, "{", 1);
}

void jw_str(jw_t *w, const char *k, const char *val)
{
    char tmp[512];
    key(w, k);
    size_t n = json_escape(tmp, sizeof tmp, val ? val : "");
    if (n >= sizeof tmp) {
        w->overflow = true;
        return;
    }
    put(w, tmp, n);
}

void jw_num(jw_t *w, const char *k, double v)
{
    char tmp[32];
    key(w, k);
    if (!isfinite(v)) {
        put(w, "null", 4);
        return;
    }
    int n = snprintf(tmp, sizeof tmp, "%.10g", v);
    /* inteiro vira "12.0" como no Python (float), para o app ler como Double */
    if (!strpbrk(tmp, ".eEn")) n += snprintf(tmp + n, sizeof tmp - (size_t)n, ".0");
    put(w, tmp, (size_t)n);
}

void jw_int(jw_t *w, const char *k, long v)
{
    char tmp[24];
    key(w, k);
    int n = snprintf(tmp, sizeof tmp, "%ld", v);
    put(w, tmp, (size_t)n);
}

void jw_bool(jw_t *w, const char *k, bool v)
{
    key(w, k);
    if (v)
        put(w, "true", 4);
    else
        put(w, "false", 5);
}

void jw_raw(jw_t *w, const char *k, const char *raw, size_t len)
{
    key(w, k);
    put(w, raw, len);
}

size_t jw_end(jw_t *w)
{
    put(w, "}", 1);
    return w->overflow ? 0 : w->len;
}
