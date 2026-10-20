// SPDX-License-Identifier: GPL-3.0-only
#include "beacon.h"

#include <gccore.h>
#include <network.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Every net_write is copied through libogc's small network heap, so large sends go in pieces.
#define CHUNK 2048

static s32 sock = -1;
static char tour[32];

static int send_all(const void *data, int n) {
    const u8 *p = data;
    while (n > 0 && sock >= 0) {
        s32 w = net_write(sock, p, n > CHUNK ? CHUNK : n);
        if (w <= 0) {
            net_close(sock);
            sock = -1;
            return -1;
        }
        p += w;
        n -= w;
    }
    return sock >= 0 ? 0 : -1;
}

// wiiload hands its first argument over as argv[0], so every slot is searched.
int beacon_start(int argc, char **argv) {
    char target[48] = "";
    for (int i = 0; i < argc; i++) {
        if (!argv[i]) continue;
        if (!strncmp(argv[i], "beacon=", 7)) snprintf(target, sizeof target, "%s", argv[i] + 7);
        if (!strncmp(argv[i], "tour=", 5)) snprintf(tour, sizeof tour, "%s", argv[i] + 5);
    }
    char *colon = strchr(target, ':');
    if (!colon) return -1;
    *colon = 0;
    // A real Wii's net_init fails now and then at boot; synchronous retries are the proven way.
    s32 rc = -1;
    for (int i = 0; i < 5 && rc < 0; i++) {
        rc = net_init();
        if (rc < 0) usleep(500000);
    }
    if (rc < 0) return -1;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_len = sizeof sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u16)atoi(colon + 1));
    sa.sin_addr.s_addr = inet_addr(target);
    sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock >= 0 && net_connect(sock, (struct sockaddr *)&sa, sizeof sa) < 0) {
        net_close(sock);
        sock = -1;
    }
    if (sock < 0) {
        net_deinit();
        return -1;
    }
    beacon_log("hello tour=%s", tour[0] ? tour : "none");
    return 0;
}

const char *beacon_tour(void) {
    return tour;
}

void beacon_log(const char *format, ...) {
    if (sock < 0) return;
    char line[512];
    int n = snprintf(line, sizeof line, "LOG ");
    va_list args;
    va_start(args, format);
    n += vsnprintf(line + n, sizeof line - (size_t)n - 1, format, args);
    va_end(args);
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    send_all(line, n);
}

void beacon_shot(const char *name, const void *xfb, int width, int height) {
    if (sock < 0) return;
    char head[96];
    int bytes = width * height * 2;
    int n = snprintf(head, sizeof head, "SHOT %s %d %d %d\n", name, width, height, bytes);
    if (send_all(head, n) == 0) send_all(xfb, bytes);
}

void beacon_stop(void) {
    if (sock < 0) return;
    send_all("END\n", 4);
    net_close(sock);
    sock = -1;
    net_deinit();
}
