// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SETTINGS_H
#define SETTINGS_H

#include "compose.h"

typedef enum { SEED_RANDOM, SEED_LAST, SEED_TYPED, SEED_MODES } SeedMode;
typedef enum { AFTER_CARD, AFTER_NEXT, AFTER_REPLAY, AFTER_MODES } AfterPiece;
typedef enum { GLOW_OFF, GLOW_SOFT, GLOW_STRONG, GLOW_LEVELS } GlowLevel;

#define VOLUME_STEPS 10
#define MARGIN_MAX 8

typedef struct {
    SeedMode seed_mode;
    char typed_seed[SEED_MAX + 1];
    // MOOD_ANY, or one mood every voyage.
    int mood;
    LengthPref length;
    AfterPiece after;
    int volume;
    // Reduce flashing and reduce motion.
    int calm, still;
    GlowLevel glow;
    // Percent of the screen kept clear at each edge, for TVs that crop.
    int margin;
    int save_last_seed;
} Settings;

void settings_defaults(Settings *s);
// Reads "key = value" lines over the defaults; unknown keys and bad values are ignored.
void settings_parse(Settings *s, const char *text);
int settings_format(const Settings *s, char *out, int size);

#endif
