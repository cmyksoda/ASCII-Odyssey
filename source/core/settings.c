// SPDX-License-Identifier: GPL-3.0-only
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const SEED_NAMES[SEED_MODES] = {"random", "last", "typed"};
static const char *const AFTER_NAMES[AFTER_MODES] = {"card", "next", "replay"};
static const char *const LENGTH_NAMES[3] = {"short", "medium", "long"};
static const char *const GLOW_NAMES[GLOW_LEVELS] = {"off", "soft", "strong"};

void settings_defaults(Settings *s) {
    memset(s, 0, sizeof *s);
    s->mood = MOOD_ANY;
    s->length = LENGTH_MEDIUM;
    s->volume = VOLUME_STEPS;
    s->glow = GLOW_SOFT;
    s->save_last_seed = 1;
}

static int named(const char *value, const char *const *names, int count, int fallback) {
    for (int i = 0; i < count; i++)
        if (!strcmp(value, names[i])) return i;
    return fallback;
}

static int number(const char *value, int lo, int hi, int fallback) {
    char *end;
    long n = strtol(value, &end, 10);
    return end != value && !*end && n >= lo && n <= hi ? (int)n : fallback;
}

static void apply(Settings *s, const char *key, const char *value) {
    if (!strcmp(key, "seed")) s->seed_mode = (SeedMode)named(value, SEED_NAMES, SEED_MODES, s->seed_mode);
    if (!strcmp(key, "typed_seed")) seed_normalize(value, s->typed_seed, sizeof s->typed_seed);
    if (!strcmp(key, "mood")) s->mood = !strcmp(value, "any") ? MOOD_ANY : named(value, mood_names, MOOD_COUNT, s->mood);
    if (!strcmp(key, "length")) s->length = (LengthPref)named(value, LENGTH_NAMES, 3, s->length);
    if (!strcmp(key, "after")) s->after = (AfterPiece)named(value, AFTER_NAMES, AFTER_MODES, s->after);
    if (!strcmp(key, "volume")) s->volume = number(value, 0, VOLUME_STEPS, s->volume);
    if (!strcmp(key, "reduce_flashing")) s->calm = number(value, 0, 1, s->calm);
    if (!strcmp(key, "reduce_motion")) s->still = number(value, 0, 1, s->still);
    if (!strcmp(key, "glow")) s->glow = (GlowLevel)named(value, GLOW_NAMES, GLOW_LEVELS, s->glow);
    if (!strcmp(key, "margin")) s->margin = number(value, 0, MARGIN_MAX, s->margin);
    if (!strcmp(key, "save_last_seed")) s->save_last_seed = number(value, 0, 1, s->save_last_seed);
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = 0;
    return s;
}

void settings_parse(Settings *s, const char *text) {
    char line[128];
    while (*text) {
        int n = 0;
        while (*text && *text != '\n') {
            if (n < (int)sizeof line - 1) line[n++] = *text;
            text++;
        }
        if (*text) text++;
        line[n] = 0;
        char *eq = strchr(line, '=');
        if (line[0] == '#' || !eq) continue;
        *eq = 0;
        apply(s, trim(line), trim(eq + 1));
    }
    // A typed seed that went missing can't be sailed to.
    if (s->seed_mode == SEED_TYPED && !s->typed_seed[0]) s->seed_mode = SEED_RANDOM;
}

int settings_format(const Settings *s, char *out, int size) {
    return snprintf(out, (size_t)size,
                    "# ASCII Odyssey settings\n"
                    "seed = %s\ntyped_seed = %s\nmood = %s\nlength = %s\nafter = %s\nvolume = %d\n"
                    "reduce_flashing = %d\nreduce_motion = %d\nglow = %s\nmargin = %d\nsave_last_seed = %d\n",
                    SEED_NAMES[s->seed_mode], s->typed_seed, s->mood < 0 ? "any" : mood_names[s->mood],
                    LENGTH_NAMES[s->length], AFTER_NAMES[s->after], s->volume, s->calm, s->still, GLOW_NAMES[s->glow],
                    s->margin, s->save_last_seed);
}
