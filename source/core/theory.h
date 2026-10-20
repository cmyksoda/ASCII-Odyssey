// SPDX-License-Identifier: GPL-3.0-only
#ifndef THEORY_H
#define THEORY_H

typedef enum {
    SCALE_MAJOR,
    SCALE_MINOR,
    SCALE_DORIAN,
    SCALE_PHRYGIAN,
    SCALE_LYDIAN,
    SCALE_MIXOLYDIAN,
    SCALE_HARMONIC_MINOR,
    SCALE_COUNT
} Scale;

typedef struct {
    // Pitch class of the tonic, 0 = C.
    int tonic;
    Scale scale;
} Key;

// A chord built on a scale degree (0-based) by stacking the scale's own thirds.
typedef enum { EXT_TRIAD, EXT_SEVENTH, EXT_NINTH, EXT_SUS2, EXT_SUS4, EXT_ADD9 } Extension;

typedef struct {
    int degree;
    Extension ext;
    // Semitones above the root that make it up, sorted.
    int tones[5];
    int count;
    // Absolute pitch class of the root.
    int root_pc;
} Chord;

extern const char *const scale_names[SCALE_COUNT];
extern const char *const note_names[12];

// Pitch class of a scale degree; degrees wrap into other octaves.
int key_pitch_class(Key k, int degree);
Chord chord_build(Key k, int degree, Extension ext);
int chord_has_pc(const Chord *c, int pitch_class);
// The chord tone closest to note, preferring to move down when tied.
int chord_nearest(const Chord *c, int note);
// Voices the chord inside [low, high] moving as little as possible from previous (count voices).
void chord_voice(const Chord *c, int voices, int low, int high, const int *previous, int *out);
// Moves a note up or down by scale steps.
int key_step(Key k, int note, int steps);
int chord_minor(const Chord *c);

#endif
