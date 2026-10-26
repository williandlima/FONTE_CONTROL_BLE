/* Configuracao do firmware - ponte BLE <-> RS-232 para Agilent/Keysight E3634A.
 * Valores que nao dependem da placa (pinos ficam em main/board.h). */
#ifndef CONFIG_H
#define CONFIG_H

#define FW_VERSION "2.0.1"        /* sobe a cada mudanca; aparece no app */

/* ---------------- RS-232 (manual E3634A, cap. 4 "Remote Interface Configuration") ----
 * Fonte: 9600 baud (fabrica), "NONE 8 BITS", 1 start bit, 2 stop bits.
 * TX com 2 stop bits funciona com fonte que espera 1 ou 2; RX aceita os dois. */
#define PSU_BAUD 9600
#define PSU_STOP_BITS 2
#define PSU_TIMEOUT_MS 2500       /* resposta a uma query (MEAS? leva ~100 ms) */
#define PSU_XONXOFF 1             /* igual ao Lab Test; inofensivo se a fonte nao usa */
#define PSU_COMPOUND_QUERY 0      /* 0: MEAS:VOLT:DC? / 50 ms / MEAS:CURR:DC? (validado) */
#define PSU_LABTEST_SEQUENCE 1    /* abre como o Lab Test: ^C, *CLS, SYST:ERR?, *IDN?, SYST:REM */
#define PSU_LOCK_FRONT_PANEL 0    /* 1 -> SYST:RWL (trava o painel em modo remoto) */
#define PSU_STATE_EVERY 5         /* rele OUTP?/TRIP? a cada N medicoes */

/* ---------------- BLE ---------------- */
#define BLE_NAME "E3634A-BLE"

/* ---------------- Medicao ---------------- */
#define POLL_MS 500               /* intervalo padrao de leitura V/I (alteravel pelo app) */
#define POLL_MIN_MS 300           /* duas queries seguidas a 9600 baud */
#define RECONNECT_MS 3000         /* nova tentativa com a fonte quando sem resposta */

#endif
