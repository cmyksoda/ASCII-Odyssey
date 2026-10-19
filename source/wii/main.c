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
    // Buttons to press and when, in wall seconds: "1.0:down,1.4:confirm".
    const char *script;
} Tour;

static const Tour TOURS[] = {
    {"title", 6.0, {1.0, 4.0}, 2, 0, NULL},
    {"piece", 95.0, {3.0, 9.0, 20.0, 35.0, 50.0, 65.0, 80.0}, 7, 1, "1.0:confirm"},
    // Into settings, the keyboard types "qwe" with its chimes, then Start sails to that seed.
    {"keys", 16.0, {1.7, 3.7, 4.5, 12.0}, 4, 0,
     "1.0:down,1.4:confirm,2.2:confirm,2.6:confirm,2.8:right,3.0:confirm,3.2:right,3.4:confirm,3.9:pause,4.2:back,"
     "4.7:up,5.0:confirm"},
    // Start on the first press, then the tracker view, then the info view.
    {"overlays", 20.0, {1.6, 9.0, 15.0}, 3, 0, "1.0:confirm,6.0:one,12.0:two"},
};

// Presses a tour's scripted buttons as their moments come, one per frame, the way a person with a remote would.
static unsigned scripted(const char **cursor, double wall) {
    static const struct {
        const char *name;
        unsigned bit;
    } names[] = {{"up", INPUT_UP},           {"down", INPUT_DOWN}, {"left", INPUT_LEFT},   {"right", INPUT_RIGHT},
                 {"confirm", INPUT_CONFIRM}, {"back", INPUT_BACK}, {"pause", INPUT_PAUSE}, {"one", INPUT_ALT1},
                 {"two", INPUT_ALT2}};
    if (!*cursor || !**cursor) return 0;
    char *end;
    double at = strtod(*cursor, &end);
    if (at > wall || *end != ':') return 0;
    const char *name = end + 1;
    int n = (int)strcspn(name, ",");
    unsigned pressed = 0;
    for (unsigned k = 0; k < sizeof names / sizeof names[0]; k++)
        if ((int)strlen(names[k].name) == n && !strncmp(name, names[k].name, (size_t)n)) pressed |= names[k].bit;
    *cursor = name[n] ? name + n + 1 : name + n;
    return pressed;
}

typedef struct {
    unsigned frames, late;
    double app_us, build_us, gpu_us, worst_us;
    double step_min, step_max;
} Stats;

static void stats_add(Stats *s, double app, double build, double gpu, double frame, int late, double music_step) {
    if (!s->frames) s->step_min = 1e9;
    s->frames++;
    s->app_us += app;
    s->build_us += build;
    s->gpu_us += gpu;
    if (frame > s->worst_us) s->worst_us = frame;
    if (late) s->late++;
    if (music_step > 0) {
        if (music_step < s->step_min) s->step_min = music_step;
        if (music_step > s->step_max) s->step_max = music_step;
    }
}

static void stats_report(Stats *s) {
    if (!s->frames) return;
    beacon_log("perf frames=%u app=%.2fms build=%.2fms gpu=%.2fms worst=%.2fms late=%u", s->frames,
               s->app_us / s->frames / 1000, s->build_us / s->frames / 1000, s->gpu_us / s->frames / 1000,
               s->worst_us / 1000, s->late);
    if (s->step_max > 0) beacon_log("music steps %.1f..%.1fms", s->step_min * 1000, s->step_max * 1000);
    char zones[256];
    prof_report(zones, sizeof zones);
    if (zones[0]) beacon_log("zones %s", zones);
    AudioStatus a;
    audio_status(&a);
    beacon_log("audio load=%.1f%% max=%.1f%% restarts=%u refusals=%u", a.load_avg * 100, a.load_max * 100, a.restarts,
               a.refusals);
    memset(s, 0, sizeof *s);
}

/* The DSP's play counter moves in 21 ms blocks; a refresh-clock estimate that leans gently toward it moves evenly. */

typedef struct {
    double music, wall;
    int locked;
} MusicClock;

static double music_clock(MusicClock *m, double heard, double refresh, int running) {
    if (!running || !m->locked) {
        m->music = heard;
        m->wall = refresh;
        m->locked = running;
        return heard;
    }
    double guess = m->music + (refresh - m->wall), error = heard - guess;
    m->music = error > 0.1 || error < -0.1 ? heard : guess + error * 0.04;
    m->wall = refresh;
    return m->music;
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
    audio_chimes_prepare();
    AppConfig config = {0};
    config.wide = video_wide();
    // Test tours keep their own files, so a hands-free run never changes the player's settings or last seed.
    const char *settings_file = tour ? "settings-tour.txt" : "settings.txt";
    const char *last_file = tour ? "last-seed-tour.txt" : "last_seed.txt";
    settings_defaults(&config.settings);
    char text[1024];
    if (storage_read(settings_file, text, sizeof text) > 0)
        settings_parse(&config.settings, text);
    else
        config.first_boot = !tour;
    read_seed_file(last_file, config.last_seed, sizeof config.last_seed);
    for (int i = 0; i < argc; i++) {
        if (argv[i] && !strncmp(argv[i], "seed=", 5)) seed_normalize(argv[i] + 5, config.fixed_seed, sizeof config.fixed_seed);
        for (int m = 0; argv[i] && !strncmp(argv[i], "mood=", 5) && m < MOOD_COUNT; m++)
            if (!strcmp(argv[i] + 5, mood_names[m])) config.settings.mood = m;
    }
    App *app = app_create(&config);
    if (!app) return 1;
    if (tour) beacon_log("video %s, last seed \"%s\"", video_wide() ? "16:9" : "4:3", config.last_seed);
    Player player;
    int playing = 0, paused = 0;
    MusicClock music = {0};
    double last_music = -1;
    float volume = -1;
    Piece *piece = NULL;
    u64 boot = gettime(), last = boot;
    // A shot stalls the loop while it sends, so the frame after one stays out of the timing stats.
    int shot = 0, after_shot = 0;
    const char *script = tour ? tour->script : NULL;
    // The tour's own clock stops while a shot is sending, so its script and shots keep their spacing.
    double stalled = 0;
    Stats stats = {0};
    for (;;) {
        Input in;
        input_poll(&in);
        double wall = (double)ticks_to_microsecs(gettime() - boot) / 1e6;
        if (script) in.pressed |= scripted(&script, wall - stalled);
        if (job.done) {
            if (worker_running) LWP_JoinThread(worker, NULL);
            worker_running = 0;
            job.done = 0;
            piece = job.result;
            if (piece) app_set_piece(app, piece);
            if (tour) beacon_log("composed \"%s\" in %.0f ms: %s", job.seed, job.millis,
                                 piece ? mood_names[piece->comp.info.mood] : "failed");
        }
        double heard = playing ? (double)audio_played() / AUDIO_RATE : -1;
        AppClock clock = {.wall = wall, .music = playing ? music_clock(&music, heard, video_time(), !paused && heard > 0) : -1,
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
            playing = paused = 0;
        }
        Piece *retired = app_release_piece(app);
        if (retired) {
            piece_free(retired);
            free(retired);
            if (retired == piece) piece = NULL;
        }
        if (req.compose && !worker_running && !job.done) start_compose(&req, app_viewport(app));
        if (req.save_settings) {
            char settings[1024];
            settings_format(app_settings(app), settings, sizeof settings);
            storage_write(settings_file, settings);
        }
        if (req.volume != volume) audio_volume(volume = req.volume);
        if (req.chime >= 0) audio_chime(req.chime);
        if (req.save_last_seed) {
            char line[64];
            snprintf(line, sizeof line, "%s\n", req.seed);
            storage_write(last_file, line);
        }
        if (req.start_music && piece && player_init(&player, piece->comp.song, 0) == 0) {
            audio_start(&player);
            playing = 1;
            paused = 0;
        }
        if (req.pause_music) audio_pause(paused = 1);
        if (req.resume_music) audio_pause(paused = 0);
        video_set_interval(playing || worker_running ? 2 : 1);
        int retraces = video_draw(&screen);
        if (tour) {
            unsigned build, gpu;
            video_times(&build, &gpu);
            u64 now = gettime();
            double step = playing && last_music >= 0 && !after_shot ? clock.music - last_music : -1;
            last_music = playing ? clock.music : -1;
            stats_add(&stats, ticks_to_microsecs(t1 - t0), build, gpu, ticks_to_microsecs(now - last),
                      !after_shot && retraces > video_interval(), step);
            after_shot = 0;
            last = now;
            if (stats.frames == 120) stats_report(&stats);
            double mark = tour->music_shots ? clock.music : wall - stalled;
            if (shot < tour->shot_count && mark >= tour->shots[shot]) {
                int w, h;
                const void *xfb = video_xfb(&w, &h);
                char name[32];
                snprintf(name, sizeof name, "%s-%02d", tour->name, shot);
                beacon_shot(name, xfb, w, h);
                shot++;
                after_shot = 1;
                u64 sent = gettime();
                stalled += (double)ticks_to_microsecs(sent - now) / 1e6;
                last = sent;
            }
            if (wall - stalled >= tour->length || (tour->music_shots && clock.music >= tour->shots[tour->shot_count - 1] + 3))
                break;
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
