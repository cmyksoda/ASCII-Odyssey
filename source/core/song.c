// SPDX-License-Identifier: GPL-3.0-only
#include "song.h"

#include <stdlib.h>

Song *song_create(void) {
    Song *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->patterns = calloc(MAX_PATTERNS, sizeof(Pattern));
    if (!s->patterns) {
        free(s);
        return NULL;
    }
    s->bpm = 120;
    s->speed = 6;
    s->pattern_rows = PATTERN_ROWS;
    s->channels = SONG_CHANNELS;
    return s;
}

void song_free(Song *s) {
    if (!s) return;
    for (int i = 0; i < s->instrument_count; i++)
        for (int k = 0; k < s->instruments[i].sample_count; k++) free(s->instruments[i].samples[k].frames);
    free(s->patterns);
    free(s->duck_amount);
    free(s->master_cutoff);
    free(s->level);
    free(s);
}

Pattern *song_pattern(Song *s, int index) {
    if (index < 0 || index >= MAX_PATTERNS) return NULL;
    if (index >= s->pattern_count) s->pattern_count = index + 1;
    return &s->patterns[index];
}

int song_rows(const Song *s) {
    return s->order_length * s->pattern_rows;
}

double song_row_seconds(const Song *s) {
    return 2.5 / s->bpm * s->speed;
}
