// SPDX-License-Identifier: GPL-3.0-or-later
// Settings survive a trip through settings.txt, and a damaged file falls back to defaults.
#include "check.h"
#include "core/settings.h"

#include <stdio.h>
#include <string.h>

static void round_trip(void) {
    Settings a, b;
    settings_defaults(&a);
    a.seed_mode = SEED_TYPED;
    snprintf(a.typed_seed, sizeof a.typed_seed, "paper moon");
    a.mood = MOOD_MUSICBOX;
    a.length = LENGTH_LONG;
    a.after = AFTER_NEXT;
    a.volume = 7;
    a.calm = 1;
    a.still = 1;
    a.glow = GLOW_STRONG;
    a.margin = 5;
    a.save_last_seed = 0;
    char text[512];
    settings_format(&a, text, sizeof text);
    settings_defaults(&b);
    settings_parse(&b, text);
    CHECK(b.seed_mode == SEED_TYPED);
    CHECK(!strcmp(b.typed_seed, "paper moon"));
    CHECK(b.mood == MOOD_MUSICBOX);
    CHECK(b.length == LENGTH_LONG);
    CHECK(b.after == AFTER_NEXT);
    CHECK(b.volume == 7);
    CHECK(b.calm == 1 && b.still == 1);
    CHECK(b.glow == GLOW_STRONG);
    CHECK(b.margin == 5);
    CHECK(b.save_last_seed == 0);
}

static void saved_before_genres(void) {
    Settings s;
    settings_defaults(&s);
    settings_parse(&s, "mood = dubstep\n");
    CHECK(s.mood == MOOD_DUBSTEP);
    char text[1024];
    settings_format(&s, text, sizeof text);
    CHECK(strstr(text, "genre = dubstep\n") != NULL);
}

static void damaged(void) {
    Settings s;
    settings_defaults(&s);
    settings_parse(&s,
                   "volume = 99\nmood = polka\nseed = typed\nmargin = -3\nglow\n=\n\x01\x02 junk\nlength = short\r\n");
    CHECK(s.volume == VOLUME_STEPS);
    CHECK(s.mood == MOOD_ANY);
    CHECK(s.seed_mode == SEED_RANDOM);
    CHECK(s.margin == 0);
    CHECK(s.length == LENGTH_SHORT);
}

int main(void) {
    round_trip();
    saved_before_genres();
    damaged();
    if (!failures) printf("settings: all passed\n");
    return failures ? 1 : 0;
}
