#include "ble_nus.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "nus";

/* 6E400001-B5A3-F393-E0A9-E50E24DCCA9E (bytes em ordem little-endian) */
#define NUS_UUID(b12) BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
                                       0x93, 0xf3, 0xa3, 0xb5, b12, 0x00, 0x40, 0x6e)
static const ble_uuid128_t nus_svc_uuid = NUS_UUID(0x01);
static const ble_uuid128_t nus_rx_uuid = NUS_UUID(0x02);
static const ble_uuid128_t nus_tx_uuid = NUS_UUID(0x03);

static uint16_t tx_handle;
static volatile uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static volatile uint16_t mtu = 23;
static uint8_t own_addr_type;

static MessageBufferHandle_t lines;   /* linhas completas vindas do app */
static QueueHandle_t events;          /* nus_event_t */
static char inbuf[1024];              /* linha em montagem (so no contexto do host) */
static size_t inlen;

static void advertise(void);

/* chamado na task do host NimBLE: so remonta linhas e enfileira */
static void on_rx(const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (d[i] == '\n') {
            if (inlen) xMessageBufferSend(lines, inbuf, inlen, 0);
            inlen = 0;
        } else if (inlen < sizeof inbuf) {
            inbuf[inlen++] = (char)d[i];
        } else {
            inlen = 0;                 /* protecao contra lixo sem '\n' */
        }
    }
}

static int chr_access(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t buf[512];
        uint16_t len = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof buf, &len)) return BLE_ATT_ERR_UNLIKELY;
        on_rx(buf, len);
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return 0;   /* TX lido: vazio */
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &nus_rx_uuid.u,
                .access_cb = chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &nus_tx_uuid.u,
                .access_cb = chr_access,
                .val_handle = &tx_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
            },
            {0},
        },
    },
    {0},
};

static void post(nus_event_t ev) { xQueueSend(events, &ev, 0); }

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn = event->connect.conn_handle;
            mtu = 23;
            inlen = 0;
            ESP_LOGI(TAG, "app conectado");
            post(NUS_EV_CONNECT);
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        conn = BLE_HS_CONN_HANDLE_NONE;
        inlen = 0;
        ESP_LOGI(TAG, "app desconectado (motivo %d)", event->disconnect.reason);
        post(NUS_EV_DISCONNECT);
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_MTU:
        mtu = event->mtu.value;
        ESP_LOGI(TAG, "MTU %d", event->mtu.value);
        break;
    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields f;
    memset(&f, 0, sizeof f);
    const char *name = ble_svc_gap_device_name();
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (const uint8_t *)name;
    f.name_len = (uint8_t)strlen(name);
    f.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc) ESP_LOGE(TAG, "adv_set_fields %d", rc);

    struct ble_hs_adv_fields rsp;          /* UUID do NUS na resposta de scan */
    memset(&rsp, 0, sizeof rsp);
    rsp.uuids128 = &nus_svc_uuid;
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc) ESP_LOGE(TAG, "adv_rsp_set_fields %d", rc);

    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof p);
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = BLE_GAP_ADV_ITVL_MS(200);
    p.itvl_max = BLE_GAP_ADV_ITVL_MS(250);
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc && rc != BLE_HS_EALREADY) ESP_LOGE(TAG, "adv_start %d", rc);
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    advertise();
    ESP_LOGI(TAG, "anunciando como \"%s\"", ble_svc_gap_device_name());
}

static void on_reset(int reason) { ESP_LOGW(TAG, "reset do host BLE, motivo %d", reason); }

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void nus_init(const char *name)
{
    lines = xMessageBufferCreate(4096);
    events = xQueueCreate(8, sizeof(nus_event_t));

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(svcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(svcs));
    ble_svc_gap_device_name_set(name);
    nimble_port_freertos_init(host_task);
}

bool nus_connected(void) { return conn != BLE_HS_CONN_HANDLE_NONE; }

size_t nus_read_line(char *buf, size_t cap)
{
    size_t n = xMessageBufferReceive(lines, buf, cap - 1, 0);
    buf[n] = '\0';
    return n;
}

bool nus_poll_event(nus_event_t *ev) { return xQueueReceive(events, ev, 0) == pdTRUE; }

void nus_send_line(const char *line)
{
    static uint8_t data[1024];               /* so o loop principal envia */
    size_t len = strlen(line);
    if (len > sizeof data - 1) len = sizeof data - 1;
    memcpy(data, line, len);
    data[len++] = '\n';
    size_t step = mtu > 23 ? (size_t)mtu - 3 : 20;
    for (size_t off = 0; off < len; off += step) {
        size_t n = len - off < step ? len - off : step;
        for (int tries = 0;; tries++) {
            uint16_t c = conn;
            if (c == BLE_HS_CONN_HANDLE_NONE) return;
            struct os_mbuf *om = ble_hs_mbuf_from_flat(data + off, (uint16_t)n);
            if (om && ble_gatts_notify_custom(c, tx_handle, om) == 0) break;
            if (tries >= 50) return;            /* app parou de ler: descarta */
            vTaskDelay(pdMS_TO_TICKS(5));       /* fila de notificacoes cheia */
        }
    }
}
