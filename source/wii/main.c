// SPDX-License-Identifier: GPL-3.0-only
#include "../core/app.h"
#include "beacon.h"
#include "input.h"
#include "video.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The Homebrew Channel leaves its loader stub here; without it, exit() would hang a disc-launched DOL.
static int launched_from_hbc(void) {
    return !memcmp((void *)0x80001804, "STUBHAXX", 8);
}

typedef struct {
    double length;
    const double *shots;
    int shot_count;
} Tour;

static const double title_shots[] = {1.0, 4.0};
static const Tour title_tour = {6.0, title_shots, 2};

typedef struct {
    unsigned frames, late;
    double app_us, build_us, gpu_us, worst_us;
} Stats;

static void stats_add(Stats *s, double app, double build, double gpu, double frame) {
    s->frames++;
    s->app_us += app;
    s->build_us += build;
    s->gpu_us += gpu;
    if (frame > s->worst_us) s->worst_us = frame;
    if (frame > 17500) s->late++;
}

static void stats_report(Stats *s) {
    if (!s->frames) return;
    beacon_log("perf frames=%u app=%.2fms build=%.2fms gpu=%.2fms worst=%.2fms late=%u", s->frames,
               s->app_us / s->frames / 1000, s->build_us / s->frames / 1000, s->gpu_us / s->frames / 1000,
               s->worst_us / 1000, s->late);
    memset(s, 0, sizeof *s);
}

int main(int argc, char **argv) {
    SYS_STDIO_Report(true);
    if (video_init() != 0) return 1;
    input_init();
    const Tour *tour = NULL;
    if (beacon_start(argc, argv) == 0) tour = &title_tour;
    app_boot();
    AppConfig config = {0};
    config.wide = video_wide();
    App *app = app_create(&config);
    if (!app) return 1;
    if (tour) beacon_log("video %s wide=%d", video_wide() ? "16:9" : "4:3", config.wide);
    u64 start = gettime(), last = start;
    int shot = 0;
    Stats stats = {0};
    for (;;) {
        Input in;
        input_poll(&in);
        u64 t0 = gettime();
        double t = (double)ticks_to_microsecs(t0 - start) / 1e6;
        Screen screen = {0};
        if (app_frame(app, t, &in, &screen)) break;
        u64 t1 = gettime();
        video_draw(&screen);
        if (!tour) continue;
        unsigned build, gpu;
        video_times(&build, &gpu);
        u64 now = gettime();
        stats_add(&stats, ticks_to_microsecs(t1 - t0), build, gpu, ticks_to_microsecs(now - last));
        last = now;
        if (stats.frames == 120) stats_report(&stats);
        if (shot < tour->shot_count && t >= tour->shots[shot]) {
            int w, h;
            const void *xfb = video_xfb(&w, &h);
            char name[32];
            snprintf(name, sizeof name, "%s-%02d", beacon_tour()[0] ? beacon_tour() : "tour", shot);
            beacon_shot(name, xfb, w, h);
            shot++;
            last = gettime();
        }
        if (t >= tour->length) break;
    }
    if (tour) {
        stats_report(&stats);
        beacon_log("bye");
        beacon_stop();
    }
    int power = input_power_off();
    video_shutdown();
    input_shutdown();
    app_destroy(app);
    if (power) SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    if (!launched_from_hbc()) SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
    return 0;
}
