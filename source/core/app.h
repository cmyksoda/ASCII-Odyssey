// SPDX-License-Identifier: GPL-3.0-only
#ifndef APP_H
#define APP_H

#include "rng.h"
#include "screen.h"

typedef struct {
    int wide;
    float margin;
    char last_seed[SEED_MAX + 1];
} AppConfig;

typedef struct App App;

// Everything the app needs that isn't per-frame; safe to call once the platform is up.
void app_boot(void);
App *app_create(const AppConfig *config);
void app_destroy(App *a);
// Returns 1 once the player chooses Exit.
int app_frame(App *a, double time, const Input *in, Screen *out);

#endif
