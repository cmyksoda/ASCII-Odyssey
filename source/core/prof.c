// SPDX-License-Identifier: GPL-3.0-only
#include "prof.h"

#include <stdio.h>
#include <string.h>

#define ZONES 24
#define DEPTH 8

static double (*now)(void);
static const char *names[ZONES];
static double total[ZONES];
static int calls[ZONES], zone_count;
static int stack[DEPTH], depth;
static double started[DEPTH];

void prof_clock(double (*now_us)(void)) {
    now = now_us;
}

void prof_push(const char *zone) {
    if (!now || depth == DEPTH) return;
    int z = 0;
    while (z < zone_count && names[z] != zone) z++;
    if (z == zone_count) {
        if (zone_count == ZONES) return;
        names[zone_count++] = zone;
    }
    stack[depth] = z;
    started[depth++] = now();
}

void prof_pop(void) {
    if (!now || depth == 0) return;
    depth--;
    total[stack[depth]] += now() - started[depth];
    calls[stack[depth]]++;
}

void prof_report(char *out, int size) {
    int n = 0;
    out[0] = 0;
    for (int z = 0; z < zone_count && n < size; z++) {
        if (calls[z]) n += snprintf(out + n, (size_t)(size - n), "%s=%.2f ", names[z], total[z] / calls[z] / 1000);
        total[z] = 0;
        calls[z] = 0;
    }
}
