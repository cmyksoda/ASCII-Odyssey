// SPDX-License-Identifier: GPL-3.0-only
#include "../core/app.h"
#include "../core/prof.h"
#include "audio.h"
#include "beacon.h"
#include "input.h"
#include "storage.h"
#include "video.h"

#include <gccore.h>
#include <malloc.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double now_us(void) {
    return (double)ticks_to_microsecs(gettime());
}

// The Homebrew Channel leaves its loader stub here; without it, exit() would hang a disc-launched DOL.
static int launched_from_hbc(void) {
    return !memcmp((void *)0x80001804, "STUBHAXX", 8);
}

/* Composing takes a second or two on the Wii, so it runs beside the title screen on its own thread. */

static struct {
    char seed[SEED_MAX + 1];
    Mood mood;
    LengthPref length;
    Viewport viewport;
    Piece *result;
    volatile int done;
    double millis;
} job;
static lwp_t worker;
static int worker_running;
static unsigned char worker_stack[256 * 1024] ATTRIBUTE_ALIGN(32);

static void *compose_worker(void *arg) {
    (void)arg;
    u64 t0 = gettime();
    Piece *p = malloc(sizeof *p);
    if (p && piece_create(p, job.seed, job.mood, job.length, &job.viewport) != 0) {
        piece_free(p);
        free(p);
        p = NULL;
    }
    job.millis = ticks_to_microsecs(gettime() - t0) / 1000.0;
    job.result = p;
    job.done = 1;
    return NULL;
}

static void start_compose(const AppRequest *req, const Viewport *vp) {
    snprintf(job.seed, sizeof job.seed, "%s", req->seed);
    job.mood = req->mood;
    job.length = req->length;
    job.viewport = *vp;
    job.result = NULL;
    job.done = 0;
    worker_running = LWP_CreateThread(&worker, compose_worker, NULL, worker_stack, sizeof worker_stack, 40) == 0;
    if (!worker_running) compose_worker(NULL);
}

/* Hardware test tours, run only when wiiload passes a beacon address. */

typedef struct {
    const char *name;
    double length;
    double shots[8];
    int shot_count;
    // Music seconds at which to take shots during a piece, instead of wall seconds.
    int music_shots;
} Tour;

static const Tour TOURS[] = {
    {"title", 6.0, {1.0, 4.0}, 2, 0},
    {"piece", 95.0, {3.0, 9.0, 20.0, 35.0, 50.0, 65.0, 80.0}, 7, 1},
};

typedef struct {
    unsigned frames, late;
    double app_us, build_us, gpu_us, worst_us;
} Stats;

static void stats_add(Stats *s, double app, double build, double gpu, double frame, double budget) {
    s->frames++;
    s->app_us += app;
    s->build_us += build;
    s->gpu_us += gpu;
    if (frame > s->worst_us) s->worst_us = frame;
    if (frame > budget) s->late++;
}

static void stats_report(Stats *s) {
    if (!s->frames) return;
    beacon_log("perf frames=%u app=%.2fms build=%.2fms gpu=%.2fms worst=%.2fms late=%u", s->frames,
               s->app_us / s->frames / 1000, s->build_us / s->frames / 1000, s->gpu_us / s->frames / 1000,
               s->worst_us / 1000, s->late);
    char zones[256];
    prof_report(zones, sizeof zones);
    if (zones[0]) beacon_log("zones %s", zones);
    AudioStatus a;
    audio_status(&a);
    beacon_log("audio load=%.1f%% max=%.1f%% restarts=%u refusals=%u", a.load_avg * 100, a.load_max * 100, a.restarts,
               a.refusals);
    memset(s, 0, sizeof *s);
}

static const Tour *find_tour(void) {
    for (unsigned i = 0; i < sizeof TOURS / sizeof TOURS[0]; i++)
        if (!strcmp(beacon_tour(), TOURS[i].name)) return &TOURS[i];
    return &TOURS[0];
}

static void read_seed_file(const char *name, char *out, int size) {
    char text[64];
    if (storage_read(name, text, sizeof text) <= 0) return;
    text[strcspn(text, "\r\n")] = 0;
    seed_normalize(text, out, size);
}

int main(int argc, char **argv) {
    SYS_STDIO_Report(true);
    if (video_init() != 0) return 1;
    input_init();
    audio_init();
    storage_init();
    const Tour *tour = beacon_start(argc, argv) == 0 ? find_tour() : NULL;
    if (tour) prof_clock(now_us);
    app_boot();
    AppConfig config = {0};
    config.wide = video_wide();
    read_seed_file("last_seed.txt", config.last_seed, sizeof config.last_seed);
    for (int i = 0; i < argc; i++)
        if (argv[i] && !strncmp(argv[i], "seed=", 5)) seed_normalize(argv[i] + 5, config.fixed_seed, sizeof config.fixed_seed);
    App *app = app_create(&config);
    if (!app) return 1;
    if (tour) beacon_log("video %s, last seed \"%s\"", video_wide() ? "16:9" : "4:3", config.last_seed);
    Player player;
    int playing = 0;
    Piece *piece = NULL;
    u64 boot = gettime(), last = boot;
    int shot = 0, started_tour = 0;
    Stats stats = {0};
    for (;;) {
        Input in;
        input_poll(&in);
        double wall = (double)ticks_to_microsecs(gettime() - boot) / 1e6;
        // The piece tour presses Start once, a second after boot, like a person would.
        if (tour && tour->music_shots && !started_tour && wall >= 1.0) {
            in.pressed |= INPUT_CONFIRM;
            started_tour = 1;
        }
        if (job.done) {
            if (worker_running) LWP_JoinThread(worker, NULL);
            worker_running = 0;
            job.done = 0;
            piece = job.result;
            if (piece) app_set_piece(app, piece);
            if (tour) beacon_log("composed \"%s\" in %.0f ms: %s", job.seed, job.millis,
                                 piece ? mood_names[piece->comp.info.mood] : "failed");
        }
        AppClock clock = {.wall = wall, .music = playing ? (double)audio_played() / AUDIO_RATE : -1,
                          .entropy = (uint64_t)gettime() ^ ((uint64_t)in.held << 40)};
        Screen screen = {0};
        AppRequest req;
        u64 t0 = gettime();
        prof_push("app");
        app_frame(app, &clock, &in, &screen, &req);
        prof_pop();
        u64 t1 = gettime();
        if (req.stop_music && playing) {
            audio_stop();
            player_free(&player);
            playing = 0;
        }
        Piece *retired = app_release_piece(app);
        if (retired) {
            piece_free(retired);
            free(retired);
            if (retired == piece) piece = NULL;
        }
        if (req.compose && !worker_running && !job.done) start_compose(&req, app_viewport(app));
        if (req.save_last_seed) {
            char line[64];
            snprintf(line, sizeof line, "%s\n", req.seed);
            storage_write("last_seed.txt", line);
        }
        if (req.start_music && piece && player_init(&player, piece->comp.song, 0) == 0) {
            audio_start(&player);
            playing = 1;
        }
        if (req.pause_music) audio_pause(1);
        if (req.resume_music) audio_pause(0);
        video_set_interval(playing || worker_running ? 2 : 1);
        video_draw(&screen);
        if (tour) {
            unsigned build, gpu;
            video_times(&build, &gpu);
            u64 now = gettime();
            stats_add(&stats, ticks_to_microsecs(t1 - t0), build, gpu, ticks_to_microsecs(now - last), playing ? 36000 : 18000);
            last = now;
            if (stats.frames == 120) stats_report(&stats);
            double mark = tour->music_shots ? clock.music : wall;
            if (shot < tour->shot_count && mark >= tour->shots[shot]) {
                int w, h;
                const void *xfb = video_xfb(&w, &h);
                char name[32];
                snprintf(name, sizeof name, "%s-%02d", tour->name, shot);
                beacon_shot(name, xfb, w, h);
                shot++;
                last = gettime();
            }
            if (wall >= tour->length || (tour->music_shots && clock.music >= tour->shots[tour->shot_count - 1] + 3)) break;
        }
        if (req.quit) break;
    }
    if (playing) {
        audio_stop();
        player_free(&player);
    }
    if (worker_running) LWP_JoinThread(worker, NULL);
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
