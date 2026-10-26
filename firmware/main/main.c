/* Ponte BLE <-> RS-232 para Agilent/Keysight E3634A - ESP32-C3 Super Mini, ESP-IDF.
 *
 * Toda a logica de protocolo (core/bridge.c) e o driver SCPI (core/psu.c) sao
 * portaveis e testados no PC (tests/). Aqui fica so o que depende da placa:
 * UART, BLE, LED e o console USB de diagnostico.
 *
 * Console USB (Monitor do PlatformIO, 115200): mostra cada linha SCPI enviada
 * ('>') e recebida ('<') e aceita comandos, ver HELP abaixo. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge.h"
#include "board.h"
#include "ble_nus.h"
#include "config.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "psu.h"
#if __has_include("driver/usb_serial_jtag_vfs.h")
#include "driver/usb_serial_jtag_vfs.h"
#define USE_JTAG_VFS_DRIVER() usb_serial_jtag_vfs_use_driver()
#else
#include "esp_vfs_dev.h"
#define USE_JTAG_VFS_DRIVER() esp_vfs_usb_serial_jtag_use_driver()
#endif

static const char *TAG = "main";

static psu_t psu;
static bridge_t bridge;
static bool trace_on = true;
static uint32_t baud = PSU_BAUD;

/* ------------------------------------------------------------------- LED
 * Atividade da serial: flash curto (40 ms) a cada comando enviado a fonte; se a
 * fonte responder o flash se estende por 200 ms. So curtos = fonte muda; longos
 * = fonte respondendo. Sem app conectado: um piscar rapido a cada 2 s. */
static volatile int64_t led_until_us;

static void led_set(bool on) { gpio_set_level(LED_GPIO, LED_ACTIVE_LOW ? !on : on); }

static void led_timer(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (now < led_until_us)
        led_set(true);
    else if (!nus_connected())
        led_set((now / 1000) % 2000 < 50);
    else
        led_set(false);
}

static void led_pulse(uint32_t ms)
{
    int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    if (end > led_until_us) led_until_us = end;
    led_set(true);
}

/* ----------------------------------------------------------- E/S da fonte */
static void io_write(void *c, const uint8_t *d, size_t n) { uart_write_bytes(PSU_UART_NUM, d, n); }

static size_t io_read(void *c, uint8_t *d, size_t max)
{
    size_t avail = 0;
    if (uart_get_buffered_data_len(PSU_UART_NUM, &avail) != ESP_OK || !avail) return 0;
    int n = uart_read_bytes(PSU_UART_NUM, d, avail < max ? avail : max, 0);
    return n > 0 ? (size_t)n : 0;
}

static uint32_t io_now(void *c) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void io_sleep(void *c, uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay(t ? t : 1);
}

/* MAX3232 inverte: linha RS-232 VERDADEIRA (+V) = nivel TTL BAIXO */
static int io_dtr_ok(void *c) { return gpio_get_level(PSU_DTR_IN_GPIO) == 0; }

static void io_activity(void *c, bool rx) { led_pulse(rx ? 200 : 40); }

static void io_trace(void *c, char dir, const char *line)
{
    if (trace_on) printf("%c %s\n", dir, line);
}

/* --------------------------------------------------------- saida p/ o app */
static void app_send(void *ctx, const char *line)
{
    nus_send_line(line);
    /* no console: tudo, menos a medicao periodica (so com TRACE ligado) */
    if (trace_on || !strstr(line, "\"t\":\"m\"")) printf("APP< %s\n", line);
}

/* ---------------------------------------------------------------- UART */
static void uart_setup(uint32_t b)
{
    uart_config_t cfg = {
        .baud_rate = (int)b,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = PSU_STOP_BITS == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_is_driver_installed(PSU_UART_NUM)) uart_driver_delete(PSU_UART_NUM);
    ESP_ERROR_CHECK(uart_driver_install(PSU_UART_NUM, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(PSU_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(PSU_UART_NUM, PSU_TX_GPIO, PSU_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    gpio_pullup_en(PSU_RX_GPIO);          /* RX ocioso em nivel alto se o modulo soltar */
    baud = b;
}

/* ------------------------------------------------------ console USB (debug) */
static const char HELP[] =
    "Console E3634A-BLE " FW_VERSION "\n"
    "  {json}        comando do app (ex.: {\"cmd\":\"meas\"})\n"
    "  *IDN?         qualquer SCPI: com '?' mostra a resposta, sem '?' confere SYST:ERR?\n"
    "  link          nova tentativa de conexao com a fonte\n"
    "  diag          Ctrl-C + *IDN? e mostra os bytes crus que voltarem (1,5 s)\n"
    "  edges         conta pulsos no pino RX sem decodificar nada (independe de baud/paridade)\n"
    "  loop          teste de loopback (jumper nos pinos 2-3 do DB9 do modulo, sem a fonte)\n"
    "  baud N        muda a velocidade (300..9600); deve ser igual a da fonte\n"
    "  trace         liga/desliga o log das linhas SCPI\n"
    "  help          esta ajuda\n";

static void dump_raw(uint32_t ms)
{
    uint8_t b[64];
    size_t total = 0;
    int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    while (esp_timer_get_time() < end) {
        int n = uart_read_bytes(PSU_UART_NUM, b, sizeof b, pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            printf("%02X%c", b[i], (b[i] >= 0x20 && b[i] < 0x7f) ? '(' : ' ');
            if (b[i] >= 0x20 && b[i] < 0x7f) printf("%c) ", b[i]);
        }
        if (n > 0) total += (size_t)n;
    }
    printf("\n%u bytes recebidos\n", (unsigned)total);
}

static void cmd_diag(void)
{
    printf("RX (GPIO%d) ocioso: nivel %d (esperado 1)\n", PSU_RX_GPIO, gpio_get_level(PSU_RX_GPIO));
    uart_flush_input(PSU_UART_NUM);
    uart_write_bytes(PSU_UART_NUM, "\x03", 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    uart_flush_input(PSU_UART_NUM);
    uart_write_bytes(PSU_UART_NUM, "*IDN?\r\n", 7);
    printf("> *IDN? (%lu baud, 8N%d)\n", (unsigned long)baud, PSU_STOP_BITS);
    dump_raw(1500);
    printf("0 bytes: a fonte nao transmite -> DSR (pino 6) nao esta em +5 V, jumper 4-6,\n"
           "         fio do pino 3, ou fonte em GPIB. Bytes sem texto: baud/paridade.\n");
}

static void cmd_loop(void)
{
    uart_flush_input(PSU_UART_NUM);
    uart_write_bytes(PSU_UART_NUM, "LOOP-TEST\n", 10);
    uint8_t b[32] = {0};
    int n = uart_read_bytes(PSU_UART_NUM, b, 10, pdMS_TO_TICKS(200));
    bool ok = n == 10 && !memcmp(b, "LOOP-TEST\n", 10);
    printf("loopback: %s (%d bytes)\n", ok ? "OK" : "FALHOU", n < 0 ? 0 : n);
    if (!ok)
        printf("Confira 3V3/GND do modulo, o jumper 2-3 no DB9 e TX=GPIO%d RX=GPIO%d.\n",
               PSU_TX_GPIO, PSU_RX_GPIO);
}

/* Conta transicoes no pino RX, lendo o nivel direto do GPIO (sem passar pela UART,
 * sem decodificar nada). Fatiado em pedacos de 19 ms com vTaskDelay(1) entre eles
 * para nao travar o watchdog durante a espera. */
static int count_edges(uint32_t total_ms)
{
    int last = gpio_get_level(PSU_RX_GPIO);
    int n = 0;
    for (uint32_t done = 0; done < total_ms; done += 20) {
        int64_t end = esp_timer_get_time() + 19 * 1000;
        while (esp_timer_get_time() < end) {
            int v = gpio_get_level(PSU_RX_GPIO);
            if (v != last) {
                n++;
                last = v;
            }
        }
        vTaskDelay(1);
    }
    return n;
}

static void cmd_edges(void)
{
    printf("ocioso (1 s): ");
    int idle = count_edges(1000);
    printf("%d transicoes, nivel final %d (esperado 0 e 1)\n", idle, gpio_get_level(PSU_RX_GPIO));

    uart_flush_input(PSU_UART_NUM);
    uart_write_bytes(PSU_UART_NUM, "\x03", 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    uart_write_bytes(PSU_UART_NUM, "SYST:REM\r\n", 10);
    vTaskDelay(pdMS_TO_TICKS(200));
    uart_write_bytes(PSU_UART_NUM, "*IDN?\r\n", 7);
    printf("apos *IDN? (1,5 s): ");
    int act = count_edges(1500);
    printf("%d transicoes, nivel final %d\n", act, gpio_get_level(PSU_RX_GPIO));

    if (act > 4)
        printf("HA ATIVIDADE no pino RX (GPIO%d): a fonte esta transmitindo. Se 'diag' nao\n"
               "decodifica nada legivel, o problema e baud/paridade/nivel eletrico, nao o cabo.\n",
               PSU_RX_GPIO);
    else
        printf("NADA chega ao pino RX (GPIO%d), nem um pulso: a fonte nao esta transmitindo ate\n"
               "aqui. Suspeitos: DSR (pino 6) nao esta realmente em +5V no pino certo, fio do\n"
               "pino 3 do cabo rompido/no pino errado, GND nao ligado, ou a fonte travada\n"
               "(religue-a). Isso NAO depende de baud/paridade/firmware.\n",
               PSU_RX_GPIO);
}

static void console_line(char *s)
{
    while (*s == ' ') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r')) s[--n] = '\0';
    if (!n) return;
    if (s[0] == '{') {
        bridge_handle_line(&bridge, s);
    } else if (!strcmp(s, "help") || !strcmp(s, "?")) {
        printf("%s", HELP);
    } else if (!strcmp(s, "trace")) {
        trace_on = !trace_on;
        printf("trace %s\n", trace_on ? "ligado" : "desligado");
    } else if (!strcmp(s, "link")) {
        printf("%s\n", bridge_try_link(&bridge) ? "fonte conectada" : bridge.last_err);
    } else if (!strcmp(s, "diag")) {
        cmd_diag();
    } else if (!strcmp(s, "loop")) {
        cmd_loop();
    } else if (!strcmp(s, "edges")) {
        cmd_edges();
    } else if (!strncmp(s, "baud ", 5)) {
        uart_setup((uint32_t)atoi(s + 5));
        printf("baud = %lu\n", (unsigned long)baud);
        bridge.link = false;          /* reabre a sessao ja na proxima volta */
        bridge.t_link_set = false;
    } else {
        char r[256];
        bool q = strchr(s, '?') != NULL;
        int rc = q ? psu_query(&psu, s, r, sizeof r, 0) : psu_write(&psu, s, true);
        if (rc == PSU_OK)
            printf("%s\n", q ? r : "OK");
        else if (rc == PSU_ESCPI)
            printf("erro SCPI %d: %s\n", psu.scpi_code, psu.scpi_msg);
        else
            printf("sem resposta: %s\n", psu.err);
    }
}

static void console_poll(void)
{
    static char line[512];
    static size_t len;
    uint8_t b[64];
    int n = usb_serial_jtag_read_bytes(b, sizeof b, 0);
    for (int i = 0; i < n; i++) {
        if (b[i] == '\n' || b[i] == '\r') {
            line[len] = '\0';
            if (len) console_line(line);
            len = 0;
        } else if (len < sizeof line - 1) {
            line[len++] = (char)b[i];
        }
    }
}

/* ---------------------------------------------------------------- main */
void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    usb_serial_jtag_driver_config_t ucfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&ucfg));
    USE_JTAG_VFS_DRIVER();
    setvbuf(stdout, NULL, _IONBF, 0);

    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    led_set(false);
    const esp_timer_create_args_t targs = {.callback = led_timer, .name = "led"};
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&targs, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 10 * 1000));

    uart_setup(PSU_BAUD);

    psu_io_t io = {
        .ctx = NULL,
        .write = io_write,
        .read = io_read,
        .now_ms = io_now,
        .sleep_ms = io_sleep,
        .dtr_ok = NULL,
        .activity = io_activity,
        .trace = io_trace,
    };
#if PSU_DTR_IN_GPIO >= 0
    gpio_reset_pin(PSU_DTR_IN_GPIO);
    gpio_set_direction(PSU_DTR_IN_GPIO, GPIO_MODE_INPUT);
    io.dtr_ok = io_dtr_ok;
#else
    (void)io_dtr_ok;
#endif
#if PSU_DSR_OUT_GPIO >= 0
    gpio_reset_pin(PSU_DSR_OUT_GPIO);
    gpio_set_direction(PSU_DSR_OUT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(PSU_DSR_OUT_GPIO, 0);   /* nivel baixo -> DSR VERDADEIRO na fonte */
#endif
    psu_init(&psu, &io, PSU_TIMEOUT_MS);
    psu.xonxoff = PSU_XONXOFF;
    psu.compound = PSU_COMPOUND_QUERY;
    psu.state_every = PSU_STATE_EVERY;

    bridge_init(&bridge, &psu, app_send, NULL);
    nus_init(BLE_NAME);

    printf("\nE3634A-BLE firmware %s | UART%d TX=GPIO%d RX=GPIO%d %lu 8N%d | BLE \"%s\"\n"
           "Digite help no console para os comandos de diagnostico.\n",
           FW_VERSION, PSU_UART_NUM, PSU_TX_GPIO, PSU_RX_GPIO, (unsigned long)PSU_BAUD,
           PSU_STOP_BITS, BLE_NAME);
    ESP_LOGI(TAG, "pronto");

    bool link = false;
    for (;;) {
        nus_event_t ev;
        while (nus_poll_event(&ev)) bridge_on_client(&bridge, ev == NUS_EV_CONNECT);

        char line[768];
        while (nus_read_line(line, sizeof line)) {
            if (trace_on) printf("APP> %s\n", line);
            bridge_handle_line(&bridge, line);
        }
        console_poll();
        bridge_tick(&bridge);

        if (bridge.link != link) {
            link = bridge.link;
            if (link)
                ESP_LOGI(TAG, "fonte conectada: %s", psu.idn);
            else
                ESP_LOGW(TAG, "fonte sem resposta: %s", bridge.last_err);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
