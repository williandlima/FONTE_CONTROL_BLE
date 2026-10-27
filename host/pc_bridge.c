/* pc_bridge: o MESMO nucleo do firmware (firmware/core) rodando no PC.
 *
 * Fala com a fonte por uma porta serial de verdade (adaptador USB-RS232, como o
 * Lab Test) e faz o papel do app: le linhas JSON da entrada padrao e escreve as
 * respostas JSON na saida padrao. O log SCPI ('>' enviado, '<' recebido) vai
 * para a saida de erro.
 *
 *   pc_bridge COM5                 (Windows)
 *   pc_bridge /dev/ttyUSB0         (Linux)
 *   opcoes: --baud 9600  --no-labtest  --quiet  --no-poll
 *
 * Digite, por exemplo:  {"cmd":"meas"}   {"cmd":"apply","v":5,"a":0.1}
 * Se funcionar aqui e nao no ESP32, o firmware esta certo e a diferenca e
 * eletrica (DSR/pino 6, jumper 4-6, MAX3232). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge.h"
#include "config.h"
#include "psu.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#endif

static bool quiet;

/* ------------------------------------------------------------ porta serial */
#ifdef _WIN32
static HANDLE port = INVALID_HANDLE_VALUE;

static bool port_open(const char *name, unsigned baud)
{
    char path[64];
    snprintf(path, sizeof path, "\\\\.\\%s", name);
    port = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (port == INVALID_HANDLE_VALUE) return false;
    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    GetCommState(port, &dcb);
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = PSU_STOP_BITS == 2 ? TWOSTOPBITS : ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;   /* DTR do PC -> DSR da fonte VERDADEIRO */
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;                      /* XON/XOFF tratado pelo nucleo */
    dcb.fInX = FALSE;
    if (!SetCommState(port, &dcb)) return false;
    COMMTIMEOUTS to = {MAXDWORD, 0, 0, 0, 2000};   /* leitura nao bloqueante */
    SetCommTimeouts(port, &to);
    PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

static void port_write(const uint8_t *d, size_t n)
{
    DWORD w;
    WriteFile(port, d, (DWORD)n, &w, NULL);
}

static size_t port_read(uint8_t *d, size_t max)
{
    DWORD r = 0;
    if (!ReadFile(port, d, (DWORD)max, &r, NULL)) return 0;
    return r;
}

static uint32_t now_ms(void) { return (uint32_t)GetTickCount(); }
static void sleep_ms(uint32_t ms) { Sleep(ms); }

/* stdin numa thread: o loop principal nunca bloqueia */
static CRITICAL_SECTION lock;
static char inq[8192];
static size_t inq_len;
static volatile bool in_eof;

static DWORD WINAPI stdin_thread(LPVOID arg)
{
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        EnterCriticalSection(&lock);
        size_t n = strlen(line);
        if (inq_len + n < sizeof inq) {
            memcpy(inq + inq_len, line, n);
            inq_len += n;
        }
        LeaveCriticalSection(&lock);
    }
    in_eof = true;
    return 0;
}

static void stdin_start(void)
{
    InitializeCriticalSection(&lock);
    CreateThread(NULL, 0, stdin_thread, NULL, 0, NULL);
}

static size_t stdin_take(char *buf, size_t cap)
{
    EnterCriticalSection(&lock);
    size_t n = inq_len < cap ? inq_len : cap;
    memcpy(buf, inq, n);
    memmove(inq, inq + n, inq_len - n);
    inq_len -= n;
    LeaveCriticalSection(&lock);
    return n;
}
#else
static int fd = -1;

static speed_t speed(unsigned b)
{
    switch (b) {
    case 300: return B300;
    case 600: return B600;
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 19200: return B19200;
    default: return B9600;
    }
}

static bool port_open(const char *name, unsigned baud)
{
    fd = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return false;
    struct termios t;
    if (tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        cfsetispeed(&t, speed(baud));
        cfsetospeed(&t, speed(baud));
        t.c_cflag |= CLOCAL | CREAD;
        t.c_cflag &= ~(CSIZE | PARENB | CRTSCTS);
        t.c_cflag |= CS8;
        if (PSU_STOP_BITS == 2) t.c_cflag |= CSTOPB; else t.c_cflag &= ~CSTOPB;
        t.c_iflag &= ~(IXON | IXOFF | IXANY);   /* XON/XOFF tratado pelo nucleo */
        tcsetattr(fd, TCSANOW, &t);
    }
    int bits = TIOCM_DTR | TIOCM_RTS;           /* DTR do PC -> DSR da fonte VERDADEIRO */
    ioctl(fd, TIOCMBIS, &bits);                 /* (ignora erro em pty) */
    tcflush(fd, TCIOFLUSH);
    return true;
}

static void port_write(const uint8_t *d, size_t n)
{
    while (n) {
        ssize_t w = write(fd, d, n);
        if (w > 0) {
            d += w;
            n -= (size_t)w;
        } else if (w < 0 && errno != EAGAIN && errno != EINTR) {
            return;
        }
    }
    tcdrain(fd);
}

static size_t port_read(uint8_t *d, size_t max)
{
    ssize_t r = read(fd, d, max);
    return r > 0 ? (size_t)r : 0;
}

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static bool in_eof;

static void stdin_start(void) { fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK); }

static size_t stdin_take(char *buf, size_t cap)
{
    ssize_t r = read(0, buf, cap);
    if (r == 0) in_eof = true;
    return r > 0 ? (size_t)r : 0;
}
#endif

/* ------------------------------------------------------------- nucleo */
static void io_write(void *c, const uint8_t *d, size_t n) { port_write(d, n); }
static size_t io_read(void *c, uint8_t *d, size_t m) { return port_read(d, m); }
static uint32_t io_now(void *c) { return now_ms(); }
static void io_sleep(void *c, uint32_t ms) { sleep_ms(ms ? ms : 1); }

static void io_trace(void *c, char dir, const char *line)
{
    if (!quiet) fprintf(stderr, "%c %s\n", dir, line);
}

static void app_send(void *c, const char *line)
{
    fputs(line, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *name = NULL;
    unsigned baud = PSU_BAUD;
    bool labtest = PSU_LABTEST_SEQUENCE, poll = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--baud") && i + 1 < argc)
            baud = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-labtest"))
            labtest = false;
        else if (!strcmp(argv[i], "--quiet"))
            quiet = true;
        else if (!strcmp(argv[i], "--no-poll"))
            poll = false;
        else
            name = argv[i];
    }
    if (!name) {
        fprintf(stderr, "uso: pc_bridge <porta> [--baud 9600] [--no-labtest] [--quiet] [--no-poll]\n"
                        "firmware %s - exemplo: pc_bridge COM5\n", FW_VERSION);
        return 2;
    }
    if (!port_open(name, baud)) {
        fprintf(stderr, "nao abriu a porta %s\n", name);
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    fprintf(stderr, "pc_bridge firmware %s | %s %u 8N%d | digite JSON, ex.: {\"cmd\":\"meas\"}\n",
            FW_VERSION, name, baud, PSU_STOP_BITS);

    static psu_t psu;
    static bridge_t br;
    psu_io_t io = {NULL, io_write, io_read, io_now, io_sleep, NULL, NULL, io_trace};
    psu_init(&psu, &io, PSU_TIMEOUT_MS);
    psu.xonxoff = PSU_XONXOFF;
    psu.compound = PSU_COMPOUND_QUERY;
    psu.state_every = PSU_STATE_EVERY;
    bridge_init(&br, &psu, app_send, NULL);
    br.labtest = labtest;
    if (!poll) br.poll_ms = 0;

    stdin_start();
    bridge_on_client(&br, true);        /* o PC faz o papel do app conectado */

    char line[1024];
    size_t len = 0;
    for (;;) {
        char buf[256];
        size_t n = stdin_take(buf, sizeof buf);
        for (size_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                line[len] = '\0';
                bridge_handle_line(&br, line);
                len = 0;
            } else if (len < sizeof line - 1) {
                line[len++] = buf[i];
            }
        }
        if (in_eof && !n) break;
        bridge_tick(&br);
        sleep_ms(5);
    }
    return 0;
}
