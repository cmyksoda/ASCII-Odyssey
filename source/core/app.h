// SPDX-License-Identifier: GPL-3.0-only
#ifndef APP_H
#define APP_H

#include "compose.h"
#include "piece.h"
#include "rng.h"
#include "screen.h"

typedef struct {
    int wide;
    float margin;
    char last_seed[SEED_MAX + 1];
    // When set, Start sails to this seed instead of rolling a new one.
    char fixed_seed[SEED_MAX + 1];
} AppConfig;

// What the app needs the platform to do: it owns the threads, the audio and the files.
typedef struct {
    int compose;
    char seed[SEED_MAX + 1];
    Mood mood;
    LengthPref length;
    int start_music, stop_music, pause_music, resume_music;
    int save_last_seed;
    int quit;
} AppRequest;

typedef struct {
    // Seconds since boot, and of the current piece actually heard (negative when nothing plays).
    double wall, music;
    // Fresh randomness for rolling seeds.
    uint64_t entropy;
    const float *scope;
    int scope_frames;
} AppClock;

typedef struct App App;

// Everything the app needs that isn't per-frame; safe to call once the platform is up.
void app_boot(void);
App *app_create(const AppConfig *config);
void app_destroy(App *a);
void app_frame(App *a, const AppClock *clock, const Input *in, Screen *out, AppRequest *req);
// The platform hands over the piece it composed; the app draws it until it asks for the music to stop.
void app_set_piece(App *a, Piece *p);
// Lets go of the piece, so the platform can free it once its music has stopped.
Piece *app_release_piece(App *a);
const Viewport *app_viewport(const App *a);

#endif
