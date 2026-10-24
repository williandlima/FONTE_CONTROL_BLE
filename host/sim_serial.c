/* sim_serial: E3634A simulada atras de uma porta serial virtual (pty, Linux).
 *
 * Imprime o caminho da porta (ex.: /dev/pts/5) e atende como a fonte, com o
 * mesmo simulador dos testes (tests/sim). Serve para o teste de ponta a ponta
 * tests/e2e_serial.py: pc_bridge <porta>  <->  pty  <->  sim_serial.
 *
 *   sim_serial [--wiring ok|jumper|open] [--load 10]   (fecha ao fechar stdin) */
#define _XOPEN_SOURCE 600
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "sim/sim_e3634a.h"

int main(int argc, char **argv)
{
    static sim_t sim;
    sim_init(&sim);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--wiring") && i + 1 < argc) {
            const char *w = argv[++i];
            sim.wiring = !strcmp(w, "jumper") ? WIRE_JUMPER_4_6
                       : !strcmp(w, "open")   ? WIRE_DSR_OPEN
                                              : WIRE_DSR_PULLUP;
        } else if (!strcmp(argv[i], "--load") && i + 1 < argc) {
            sim.load_ohm = atof(argv[++i]);
        }
    }
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) || unlockpt(m)) {
        perror("pty");
        return 1;
    }
    struct termios t;
    tcgetattr(m, &t);
    cfmakeraw(&t);
    tcsetattr(m, TCSANOW, &t);
    printf("%s\n", ptsname(m));
    fflush(stdout);

    for (;;) {
        struct pollfd p[2] = {{m, POLLIN, 0}, {0, POLLIN, 0}};
        poll(p, 2, 2);
        if (p[1].revents & (POLLIN | POLLHUP)) {
            char c[64];
            if (read(0, c, sizeof c) <= 0) break;   /* stdin fechado: fim */
        }
        if (p[0].revents & POLLIN) {
            uint8_t b[256];
            ssize_t n = read(m, b, sizeof b);
            if (n > 0) sim_write(&sim, b, (size_t)n);
        }
        uint8_t o[256];
        size_t n = sim_read(&sim, o, sizeof o);
        if (n && write(m, o, n) < 0) break;
        if (sim.cleared || sim.log_n) {
            static int shown;
            for (; shown < sim.log_n; shown++) fprintf(stderr, "sim| %s\n", sim.log[shown]);
        }
    }
    fprintf(stderr, "sim| fim: %d comandos, %d Ctrl-C, %d beeps\n", sim.log_n, sim.cleared, sim.beeps);
    return 0;
}
