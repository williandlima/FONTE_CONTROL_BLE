/* JSON minimo para o protocolo app <-> firmware (docs/PROTOCOLO.md).
 *
 * Leitura: um objeto "plano" por linha ({"cmd":"apply","v":12,"a":0.5,"id":3}).
 * Valores aninhados (objeto/array) sao aceitos e ignorados.
 * Escrita: montagem de um objeto em um buffer fixo, sem malloc.
 */
#ifndef JSON_H
#define JSON_H

#include <stdbool.h>
#include <stddef.h>

#define JSON_MAX_KEYS 16
#define JSON_KEY_LEN 16
#define JSON_POOL 512

typedef enum { JT_STR, JT_NUM, JT_BOOL, JT_NULL, JT_OTHER } json_type_t;

typedef struct {
    char key[JSON_KEY_LEN];
    json_type_t type;
    const char *str;   /* JT_STR: texto decodificado (no pool) */
    double num;        /* JT_NUM / JT_BOOL (0/1) */
    const char *raw;   /* texto JSON original do valor (para ecoar o "id") */
    size_t raw_len;
} json_kv_t;

typedef struct {
    json_kv_t kv[JSON_MAX_KEYS];
    int n;
    char pool[JSON_POOL];
    size_t pool_used;
} json_obj_t;

/* Retorna true se 'text' e um objeto JSON valido. 'text' precisa continuar
 * valido enquanto 'raw' for usado. */
bool json_parse(json_obj_t *o, const char *text);

const json_kv_t *json_get(const json_obj_t *o, const char *key);
bool json_has(const json_obj_t *o, const char *key);
/* Verdadeiro no sentido do Python: true, numero != 0, string nao vazia. */
bool json_truthy(const json_obj_t *o, const char *key);
/* Numero ou string numerica. Retorna false se ausente ou invalido. */
bool json_number(const json_kv_t *kv, double *out);

/* ------------------------------------------------------------- escrita */
typedef struct {
    char *buf;
    size_t cap, len;
    bool first;
    bool overflow;
} jw_t;

void jw_begin(jw_t *w, char *buf, size_t cap);
void jw_str(jw_t *w, const char *key, const char *val);
void jw_num(jw_t *w, const char *key, double val);
void jw_int(jw_t *w, const char *key, long val);
void jw_bool(jw_t *w, const char *key, bool val);
void jw_raw(jw_t *w, const char *key, const char *raw, size_t len);
/* Fecha o objeto. Retorna o tamanho, ou 0 se nao coube no buffer. */
size_t jw_end(jw_t *w);

/* Escreve 's' como string JSON (com aspas) em out. Retorna o tamanho. */
size_t json_escape(char *out, size_t cap, const char *s);

#endif
