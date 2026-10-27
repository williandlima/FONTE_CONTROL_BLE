/* Testes do nucleo do firmware (C) no PC, sem hardware.
 *
 * Liga o codigo REAL do firmware (firmware/core: psu.c, bridge.c, json.c) a uma
 * E3634A simulada (tests/sim) e a um "app" falso que manda as mesmas linhas
 * JSON do app Android. O relogio e virtual: timeouts nao custam tempo real.
 *
 *   make -C tests        (ou: cc -Ifirmware/core ... ver tests/Makefile)
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge.h"
#include "config.h"
#include "json.h"
#include "psu.h"
#include "sim/sim_e3634a.h"

/* ------------------------------------------------------------ mini framework */
static int g_fail, g_checks;
static const char *g_test;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fail++; \
    printf("  FALHOU %s:%d [%s]: %s\n", __FILE__, __LINE__, g_test, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(fabs((a) - (b)) <= (tol))
#define CHECK_STR(a, b) do { g_checks++; if (strcmp((a), (b))) { g_fail++; \
    printf("  FALHOU %s:%d [%s]: \"%s\" != \"%s\"\n", __FILE__, __LINE__, g_test, (a), (b)); } } while (0)

/* ------------------------------------------------------------ bancada virtual */
typedef struct {
    sim_t sim;
    psu_t psu;
    bridge_t br;
    uint32_t clock;
    int tx_act, rx_act;
    char lines[256][768];   /* linhas que o firmware mandou ao app */
    int nlines;
} bench_t;

static bench_t B;

static void io_write(void *c, const uint8_t *d, size_t n) { sim_write(&((bench_t *)c)->sim, d, n); }
static size_t io_read(void *c, uint8_t *d, size_t m) { return sim_read(&((bench_t *)c)->sim, d, m); }
static uint32_t io_now(void *c) { return ((bench_t *)c)->clock; }
static void io_sleep(void *c, uint32_t ms) { ((bench_t *)c)->clock += ms; }
static void io_act(void *c, bool rx) { if (rx) ((bench_t *)c)->rx_act++; else ((bench_t *)c)->tx_act++; }
static void io_trace(void *c, char dir, const char *l)
{
    (void)c;
    if (getenv("TRACE")) printf("    %c %s\n", dir, l);
}

static void app_rx(void *c, const char *line)
{
    bench_t *b = c;
    if (b->nlines < 256) snprintf(b->lines[b->nlines++], sizeof b->lines[0], "%s", line);
    if (getenv("TRACE")) printf("    APP<< %s\n", line);
}

static void bench_setup(sim_wiring_t wiring)
{
    memset(&B, 0, sizeof B);
    sim_init(&B.sim);
    B.sim.wiring = wiring;
    B.clock = 1000;
    psu_io_t io = {&B, io_write, io_read, io_now, io_sleep, NULL, io_act, io_trace};
    psu_init(&B.psu, &io, PSU_TIMEOUT_MS);
    B.psu.xonxoff = PSU_XONXOFF;
    B.psu.compound = PSU_COMPOUND_QUERY;
    B.psu.state_every = PSU_STATE_EVERY;
    bridge_init(&B.br, &B.psu, app_rx, &B);
}

static void app_send(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (getenv("TRACE")) printf("    APP>> %s\n", line);
    bridge_handle_line(&B.br, line);
}

/* avanca o relogio rodando o loop principal como o firmware faz */
static void run_ms(uint32_t ms)
{
    uint32_t end = B.clock + ms;
    while ((int32_t)(end - B.clock) > 0) {
        bridge_tick(&B.br);
        B.clock += 5;
    }
}

/* ultima mensagem do tipo t (ou NULL) */
static const char *last(const char *t)
{
    char pat[32];
    snprintf(pat, sizeof pat, "\"t\":\"%s\"", t);
    for (int i = B.nlines - 1; i >= 0; i--)
        if (strstr(B.lines[i], pat)) return B.lines[i];
    return NULL;
}

static int count(const char *t)
{
    char pat[32];
    int n = 0;
    snprintf(pat, sizeof pat, "\"t\":\"%s\"", t);
    for (int i = 0; i < B.nlines; i++)
        if (strstr(B.lines[i], pat)) n++;
    return n;
}

static json_obj_t J;
static const json_obj_t *parse(const char *line)
{
    static char keep[768];
    if (!line) return NULL;
    snprintf(keep, sizeof keep, "%s", line);
    if (!json_parse(&J, keep)) return NULL;
    return &J;
}

static double fnum(const char *line, const char *k)
{
    const json_obj_t *o = parse(line);
    double x = NAN;
    if (o) json_number(json_get(o, k), &x);
    return x;
}

static bool fbool(const char *line, const char *k)
{
    const json_obj_t *o = parse(line);
    return o && json_truthy(o, k);
}

static const char *fstr(const char *line, const char *k)
{
    static char out[256];
    const json_obj_t *o = parse(line);
    const json_kv_t *kv = o ? json_get(o, k) : NULL;
    snprintf(out, sizeof out, "%s", kv && kv->type == JT_STR ? kv->str : "");
    return out;
}

/* liga a bancada: fonte responde, app conecta, recebe hello + st */
static void bench_up(void)
{
    bench_setup(WIRE_DSR_PULLUP);
    run_ms(10);
    bridge_on_client(&B.br, true);
    run_ms(20);
}

/* ================================================================== testes */

static void t_json(void)
{
    g_test = "json";
    json_obj_t o;
    char s[] = "{\"cmd\":\"apply\",\"v\":12.5,\"a\":\"0.5\",\"on\":true,\"x\":[1,{\"y\":2}],\"id\":7,"
               "\"s\":\"a\\\"b\\u0041\"}";
    CHECK(json_parse(&o, s));
    double x;
    CHECK(json_number(json_get(&o, "v"), &x) && x == 12.5);
    CHECK(json_number(json_get(&o, "a"), &x) && x == 0.5);
    CHECK(json_truthy(&o, "on"));
    CHECK(json_get(&o, "x")->type == JT_OTHER);
    CHECK_STR(json_get(&o, "s")->str, "a\"bA");
    CHECK(json_get(&o, "id")->raw_len == 1 && json_get(&o, "id")->raw[0] == '7');
    char bad1[] = "{\"cmd\":", bad2[] = "[1,2]", bad3[] = "{\"a\":1} lixo";
    CHECK(!json_parse(&o, bad1));
    CHECK(!json_parse(&o, bad2));
    CHECK(!json_parse(&o, bad3));

    char buf[128];
    jw_t w;
    jw_begin(&w, buf, sizeof buf);
    jw_str(&w, "t", "x\"y\n");
    jw_num(&w, "v", 12.0);
    jw_num(&w, "i", 0.49871);
    jw_bool(&w, "o", true);
    CHECK(jw_end(&w) > 0);
    CHECK_STR(buf, "{\"t\":\"x\\\"y\\n\",\"v\":12.0,\"i\":0.49871,\"o\":true}");
}

static void t_labtest_sequence(void)
{
    g_test = "sequencia Lab Test";
    bench_setup(WIRE_DSR_PULLUP);
    CHECK(bridge_try_link(&B.br));
    CHECK(B.sim.raw[0] == 0x03);                       /* <Ctrl-C> primeiro */
    CHECK(B.sim.log_n == 4);
    CHECK_STR(B.sim.log[0], "*CLS");
    CHECK_STR(B.sim.log[1], "SYST:ERR?");
    CHECK_STR(B.sim.log[2], "*IDN?");
    CHECK_STR(B.sim.log[3], "SYST:REM");
    CHECK(B.sim.remote);
    CHECK_STR(B.psu.idn, SIM_IDN);
    CHECK(B.sim.beeps == 0);                           /* sessao limpa, sem beep */
    /* todo comando termina em \r\n */
    CHECK(memcmp(B.sim.raw + 1, "*CLS\r\n", 6) == 0);
    CHECK(B.tx_act > 0 && B.rx_act > 0);               /* LED: TX e RX */
}

static void t_app_session(void)
{
    g_test = "sessao do app";
    bench_up();
    const char *h = last("hello");
    CHECK(h != NULL);
    CHECK_STR(fstr(h, "fw"), FW_VERSION);
    CHECK(fbool(h, "link"));
    CHECK_STR(fstr(h, "idn"), SIM_IDN);
    CHECK(fnum(h, "poll") == POLL_MS);
    const char *l = last("link");
    CHECK(l && fbool(l, "ok"));
    const char *st = last("st");
    CHECK(st != NULL);
    CHECK_STR(fstr(st, "range"), "P25V");
    CHECK(fnum(st, "ovp") == 55.0);
    CHECK_STR(fstr(st, "trig_src"), "IMM");
    /* todos os campos que o app Android le */
    const char *keys[] = {"range", "vset", "iset", "out", "ovp", "ovp_en", "ocp", "ocp_en",
                          "disp", "trig_src", "trig_del", "vtrig", "itrig", "poll"};
    for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++)
        CHECK(parse(st) && json_has(&J, keys[k]));

    /* CV: 5 V / 1 A em 10 ohm -> 5 V, 0,5 A */
    app_send("{\"cmd\":\"apply\",\"v\":5.0,\"a\":1.0,\"id\":1}");
    const char *r = last("r");
    CHECK(fbool(r, "ok") && fnum(r, "id") == 1);
    CHECK_STR(fstr(r, "cmd"), "apply");
    app_send("{\"cmd\":\"out\",\"on\":true,\"id\":2}");
    CHECK(fbool(last("r"), "ok"));
    run_ms(POLL_MS + 10);
    const char *m = last("m");
    CHECK(m != NULL);
    CHECK_NEAR(fnum(m, "v"), 5.0, 1e-6);
    CHECK_NEAR(fnum(m, "i"), 0.5, 1e-6);
    CHECK(fbool(m, "o"));
    CHECK(!fbool(m, "ovp_t") && !fbool(m, "ocp_t"));
    CHECK(fbool(last("st"), "out"));                   /* status relido apos alteracao */

    /* CC: 20 V / 1 A em 10 ohm -> 10 V, 1 A */
    app_send("{\"cmd\":\"apply\",\"v\":20,\"a\":1,\"id\":3}");
    app_send("{\"cmd\":\"meas\",\"id\":4}");
    m = last("m");
    CHECK_NEAR(fnum(m, "v"), 10.0, 1e-6);
    CHECK_NEAR(fnum(m, "i"), 1.0, 1e-6);
    CHECK(fbool(last("r"), "ok") && fnum(last("r"), "id") == 4);

    /* medicoes periodicas continuam */
    int before = count("m");
    run_ms(5 * POLL_MS);
    CHECK(count("m") >= before + 4);
    CHECK(B.sim.beeps == 0);
}

static void t_limits_and_errors(void)
{
    g_test = "limites e erros SCPI";
    bench_up();
    app_send("{\"cmd\":\"volt\",\"v\":40,\"id\":5}");          /* acima de P25V: barrado no ESP */
    const char *r = last("r");
    CHECK(!fbool(r, "ok"));
    CHECK_STR(fstr(r, "err"), "v fora da faixa (0..25.75)");
    CHECK(sim_find_log(&B.sim, "VOLT 40.0000") < 0);

    app_send("{\"cmd\":\"scpi\",\"q\":\"VOLT 40\",\"id\":6}");  /* livre: a fonte recusa */
    r = last("r");
    CHECK(!fbool(r, "ok"));
    CHECK(fnum(r, "code") == -222);
    CHECK_STR(fstr(r, "err"), "SCPI -222: Data out of range");

    app_send("{\"cmd\":\"range\",\"r\":\"HIGH\",\"id\":7}");
    CHECK(fbool(last("r"), "ok"));
    run_ms(20);                                               /* st com P50V */
    CHECK_STR(fstr(last("st"), "range"), "P50V");
    app_send("{\"cmd\":\"volt\",\"v\":40,\"id\":8}");
    CHECK(fbool(last("r"), "ok"));
    CHECK(B.sim.vset == 40.0);
    app_send("{\"cmd\":\"curr\",\"a\":5,\"id\":9}");          /* P50V: max 4,12 A */
    CHECK_STR(fstr(last("r"), "err"), "a fora da faixa (0..4.12)");

    app_send("{\"cmd\":\"range\",\"r\":\"XYZ\"}");
    CHECK_STR(fstr(last("r"), "err"), "faixa invalida");
    app_send("{\"cmd\":\"trig_src\",\"s\":\"EXT\"}");
    CHECK_STR(fstr(last("r"), "err"), "fonte de trigger BUS|IMM");
    app_send("{\"cmd\":\"save\",\"n\":9}");
    CHECK_STR(fstr(last("r"), "err"), "n fora da faixa (1..5)");
    app_send("{\"cmd\":\"apply\",\"v\":1}");
    CHECK_STR(fstr(last("r"), "err"), "campo 'a' ausente");
    app_send("{\"cmd\":\"volt\",\"v\":\"abc\"}");
    CHECK_STR(fstr(last("r"), "err"), "campo 'v' invalido");
    app_send("isso nao e json");
    CHECK_STR(fstr(last("r"), "err"), "JSON invalido");
    app_send("{\"v\":1}");
    CHECK_STR(fstr(last("r"), "err"), "faltando 'cmd'");
    app_send("{\"cmd\":\"voar\",\"id\":\"abc\"}");
    r = last("r");
    CHECK_STR(fstr(r, "err"), "comando desconhecido: voar");
    CHECK_STR(fstr(r, "id"), "abc");                          /* id string ecoado */
}

static void t_protection_memory_display(void)
{
    g_test = "protecao, memoria, display, trigger";
    bench_up();
    app_send("{\"cmd\":\"ovp\",\"v\":4.0,\"on\":true}");
    CHECK(fbool(last("r"), "ok"));
    CHECK(B.sim.ovp == 4.0 && B.sim.ovp_en);
    app_send("{\"cmd\":\"apply\",\"v\":5,\"a\":1}");
    app_send("{\"cmd\":\"out\",\"on\":true}");
    B.psu.state_n = 0;
    app_send("{\"cmd\":\"meas\"}");
    const char *m = last("m");
    CHECK(fbool(m, "ovp_t"));
    CHECK(!fbool(m, "o"));
    app_send("{\"cmd\":\"ovp\",\"on\":false}");
    app_send("{\"cmd\":\"prot_clear\"}");
    CHECK(fbool(last("r"), "ok"));
    CHECK(!B.sim.ovp_trip && !B.sim.ovp_en && B.sim.ovp == 4.0);
    app_send("{\"cmd\":\"ocp\",\"a\":8}");
    CHECK_STR(fstr(last("r"), "err"), "a fora da faixa (0..7.5)");

    app_send("{\"cmd\":\"apply\",\"v\":3.3,\"a\":0.2}");
    app_send("{\"cmd\":\"save\",\"n\":2}");
    app_send("{\"cmd\":\"reset\"}");
    CHECK(B.sim.vset == 0.0);
    app_send("{\"cmd\":\"recall\",\"n\":2}");
    CHECK_NEAR(B.sim.vset, 3.3, 1e-9);

    app_send("{\"cmd\":\"text\",\"s\":\"Ola \\\"mundo\\\" 1234567\"}");
    CHECK(fbool(last("r"), "ok"));
    CHECK_STR(B.sim.text, "Ola 'mundo' ");                    /* 12 caracteres, sem aspas */
    app_send("{\"cmd\":\"disp\",\"on\":false}");
    CHECK(!B.sim.disp);
    app_send("{\"cmd\":\"text_clear\"}");
    CHECK(B.sim.text[0] == '\0');

    app_send("{\"cmd\":\"trig_src\",\"s\":\"bus\"}");
    app_send("{\"cmd\":\"trig_lvl\",\"v\":7.5,\"a\":0.3}");
    app_send("{\"cmd\":\"trig_del\",\"s\":2}");
    app_send("{\"cmd\":\"trg\"}");
    CHECK(fbool(last("r"), "ok"));
    CHECK_NEAR(B.sim.vset, 7.5, 1e-9);
    CHECK(B.sim.trig_del == 2);

    app_send("{\"cmd\":\"scpi\",\"q\":\"MEAS:VOLT?\",\"id\":11}");
    const char *r = last("r");
    CHECK(fbool(r, "ok"));
    CHECK_STR(fstr(r, "d"), "+0.00000000E+00");               /* saida desligada pelo *RCL */
    app_send("{\"cmd\":\"scpi\",\"q\":\"   \"}");
    CHECK_STR(fstr(last("r"), "err"), "SCPI vazio");

    app_send("{\"cmd\":\"scpi\",\"q\":\"FOO:BAR\"}");         /* gera -113 */
    CHECK(fnum(last("r"), "code") == -113);
    B.sim.errq[B.sim.errn++] = -222;
    B.sim.errq[B.sim.errn++] = -221;
    app_send("{\"cmd\":\"err\",\"id\":12}");
    r = last("r");
    CHECK(strstr(r, "\"d\":[\"-222,Data out of range\",\"-221,Settings conflict\"]") != NULL);

    app_send("{\"cmd\":\"idn\"}");
    CHECK_STR(fstr(last("r"), "d"), SIM_IDN);
    app_send("{\"cmd\":\"local\"}");
    CHECK(!B.sim.remote);
    app_send("{\"cmd\":\"remote\",\"lock\":true}");
    CHECK(B.sim.remote && sim_find_log(&B.sim, "SYST:RWL") >= 0);
    app_send("{\"cmd\":\"beep\"}");
    CHECK(fbool(last("r"), "ok"));
}

static void t_local_commands(void)
{
    g_test = "comandos locais";
    bench_setup(WIRE_DSR_OPEN);                               /* fonte muda */
    app_send("{\"cmd\":\"version\",\"id\":1}");
    CHECK_STR(fstr(last("r"), "d"), FW_VERSION);
    app_send("{\"cmd\":\"ping\"}");
    CHECK_STR(fstr(last("r"), "d"), "pong");
    app_send("{\"cmd\":\"poll\",\"ms\":100}");
    CHECK(fnum(last("r"), "d") == POLL_MIN_MS);
    app_send("{\"cmd\":\"poll\",\"ms\":0}");
    CHECK(fnum(last("r"), "d") == 0);
    app_send("{\"cmd\":\"out\",\"on\":true}");
    CHECK_STR(fstr(last("r"), "err"), "fonte desconectada");
}

static void t_jumper_4_6_deadlock(void)
{
    g_test = "jumper 4-6 trava a fonte";
    bench_setup(WIRE_JUMPER_4_6);
    bridge_on_client(&B.br, true);
    run_ms(10);
    CHECK(!B.br.link);
    CHECK(strstr(B.br.last_err, "timeout rx=vazio") != NULL);
    CHECK(sim_find_log(&B.sim, "*CLS") >= 0);                 /* comandos chegam... */
    CHECK(B.rx_act == 0);                                     /* ...resposta nunca sai */
    CHECK(B.sim.out_len > 0 && !B.sim.dtr && !sim_dsr(&B.sim)); /* presa: DTR=DSR=FALSO */
    CHECK(fbool(last("hello"), "link") == false);

    /* comando mandado sem <Ctrl-C> encontra a resposta presa -> -410 + beep
     * (o sintoma visto na bancada: RMT no display, beep e nenhuma resposta) */
    psu_write(&B.psu, "*IDN?", false);
    CHECK(sim_has_error(&B.sim, -410));
    CHECK(B.sim.beeps > 0);

    /* novas tentativas sem arrumar: o <Ctrl-C> do inicio evita o -410, mas a
     * resposta volta a ficar presa */
    int beeps = B.sim.beeps;
    run_ms(RECONNECT_MS * 2 + 10);
    CHECK(B.br.fails >= 2);
    CHECK(!B.br.link);
    CHECK(B.sim.beeps == beeps);

    /* tira o jumper e liga o pino 6 em +5 V: proxima tentativa conecta sozinha,
     * porque o <Ctrl-C> do inicio da sessao descarta o que ficou preso */
    B.sim.wiring = WIRE_DSR_PULLUP;
    run_ms(30000 + 10);
    CHECK(B.br.link);
    const char *l = last("link");
    CHECK(l && fbool(l, "ok"));
    CHECK(B.sim.cleared >= 3);
}

static void t_dsr_open(void)
{
    g_test = "pino 6 solto";
    bench_setup(WIRE_DSR_OPEN);
    CHECK(!bridge_try_link(&B.br));
    CHECK_STR(B.br.last_err, "timeout rx=vazio");
    app_send("{\"cmd\":\"link\",\"id\":3}");
    const char *r = last("r");
    CHECK(!fbool(r, "ok"));
    CHECK_STR(fstr(r, "err"), "sem resposta da fonte: timeout rx=vazio");
}

static void t_backoff(void)
{
    g_test = "recuo progressivo";
    bench_setup(WIRE_DSR_OPEN);
    run_ms(10);                                     /* tentativa 1 em t=0 */
    int c1 = B.sim.cleared;
    run_ms(2900);
    CHECK(B.sim.cleared == c1);                     /* nada antes de 3 s */
    run_ms(5 * 60 * 1000);                          /* 5 min: 3,6,12,24,30,30... */
    CHECK(B.sim.cleared < 16);                      /* nao fica martelando a fonte */
    CHECK(B.sim.cleared >= 8);
}

static void t_xonxoff_and_garbage(void)
{
    g_test = "XON/XOFF e lixo";
    bench_setup(WIRE_DSR_PULLUP);
    B.sim.flow = true;
    CHECK(bridge_try_link(&B.br));
    CHECK_STR(B.psu.idn, SIM_IDN);
    psu_meas_t m;
    CHECK(psu_measure(&B.psu, &m) == PSU_OK);

    bench_setup(WIRE_DSR_PULLUP);
    B.sim.garbage = true;
    CHECK(!bridge_try_link(&B.br));
    CHECK(strstr(B.br.last_err, "rx invalido") != NULL);
}

static void t_link_drop_and_recover(void)
{
    g_test = "queda e volta da fonte";
    bench_up();
    CHECK(B.br.link);
    B.sim.alive = false;                            /* cabo puxado */
    run_ms(POLL_MS + 10);
    CHECK(B.br.link);                               /* 1 falha nao derruba */
    run_ms(4 * (POLL_MS + PSU_TIMEOUT_MS));
    CHECK(!B.br.link);
    const char *l = last("link");
    CHECK(l && !fbool(l, "ok"));
    CHECK(strstr(fstr(l, "err"), "timeout") != NULL);
    app_send("{\"cmd\":\"out\",\"on\":false}");
    CHECK_STR(fstr(last("r"), "err"), "fonte desconectada");

    B.sim.alive = true;                             /* cabo de volta */
    run_ms(RECONNECT_MS + 50);
    CHECK(B.br.link);
    run_ms(POLL_MS + 50);
    CHECK(fbool(last("link"), "ok"));
    CHECK(last("m") != NULL);
}

static void t_compound(void)
{
    g_test = "query composta";
    bench_setup(WIRE_DSR_PULLUP);
    CHECK(bridge_try_link(&B.br));
    B.psu.compound = true;
    psu_apply(&B.psu, 6, 1);
    psu_output(&B.psu, true);
    psu_meas_t m;
    CHECK(psu_measure(&B.psu, &m) == PSU_OK);
    CHECK_NEAR(m.v, 6.0, 1e-9);
    CHECK_NEAR(m.i, 0.6, 1e-9);
    CHECK(m.out);
}

int main(void)
{
    void (*tests[])(void) = {t_json, t_labtest_sequence, t_app_session, t_limits_and_errors,
                             t_protection_memory_display, t_local_commands,
                             t_jumper_4_6_deadlock, t_dsr_open, t_backoff,
                             t_xonxoff_and_garbage, t_link_drop_and_recover, t_compound};
    size_t n = sizeof tests / sizeof tests[0];
    for (size_t i = 0; i < n; i++) {
        int f0 = g_fail;
        tests[i]();
        printf("%s %s\n", g_fail == f0 ? "ok    " : "FALHOU", g_test);
    }
    printf("\n%d verificacoes, %d falhas\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
