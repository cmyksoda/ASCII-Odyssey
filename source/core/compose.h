// SPDX-License-Identifier: GPL-3.0-only
#ifndef COMPOSE_H
#define COMPOSE_H

#include "rng.h"
#include "song.h"
#include "theory.h"

typedef enum {
    MOOD_KEYGEN,
    MOOD_AMBIENT,
    MOOD_SYNTHWAVE,
    MOOD_LOFI,
    MOOD_TRANCE,
    MOOD_8BIT,
    MOOD_MUSICBOX,
    MOOD_DUB,
    MOOD_DUBSTEP,
    MOOD_COUNT,
    MOOD_ANY = -1
} Mood;

typedef enum { SECTION_INTRO, SECTION_VERSE, SECTION_BUILD, SECTION_DROP, SECTION_BREAK, SECTION_OUTRO, SECTION_KINDS } SectionKind;

typedef enum { LENGTH_SHORT, LENGTH_MEDIUM, LENGTH_LONG } LengthPref;

#define MAX_SECTIONS 16
#define MAX_BARS 192

typedef struct {
    SectionKind kind;
    int start_bar, bars;
    float energy;
    double start, end;
} Section;

typedef struct {
    double time;
    int bar;
    Chord chord;
} ChordMark;

typedef struct {
    Mood mood;
    char title[SEED_MAX + 1];
    Key key;
    // Semitones the last drop is lifted by, from lift_bar on (0 when the piece stays put).
    int lift, lift_bar;
    int bpm, beats_per_bar, rows_per_bar, bars;
    double bar_seconds, length;
    int section_count;
    Section sections[MAX_SECTIONS];
    int chord_count;
    ChordMark chords[MAX_BARS * 2];
} SongInfo;

typedef struct {
    Song *song;
    SongInfo info;
} Composition;

extern const char *const mood_names[MOOD_COUNT];
extern const char *const section_names[SECTION_KINDS];

int compose(uint64_t seed, const char *title, Mood mood, LengthPref length, Composition *out);
void composition_free(Composition *c);

#endif
