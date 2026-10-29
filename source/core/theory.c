// SPDX-License-Identifier: GPL-3.0-or-later
#include "theory.h"

#include <limits.h>
#include <stdlib.h>

static const int SCALES[SCALE_COUNT][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 11},
};

const char *const scale_names[SCALE_COUNT] = {"major", "minor", "dorian", "phrygian", "lydian", "mixolydian",
                                              "harmonic minor"};
const char *const note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static int mod(int a, int m) {
    return ((a % m) + m) % m;
}

static int degree_semitones(Scale s, int degree) {
    int octave = degree >= 0 ? degree / 7 : -((-degree + 6) / 7);
    return SCALES[s][mod(degree, 7)] + 12 * octave;
}

int key_pitch_class(Key k, int degree) {
    return mod(k.tonic + degree_semitones(k.scale, degree), 12);
}

static int key_contains(Key k, int pitch_class) {
    for (int i = 0; i < 7; i++)
        if (mod(k.tonic + SCALES[k.scale][i], 12) == mod(pitch_class, 12)) return 1;
    return 0;
}

Chord chord_build(Key k, int degree, Extension ext) {
    int root = degree_semitones(k.scale, degree);
    int third = degree_semitones(k.scale, degree + 2) - root, fifth = degree_semitones(k.scale, degree + 4) - root;
    int seventh = degree_semitones(k.scale, degree + 6) - root, ninth = degree_semitones(k.scale, degree + 8) - root;
    int second = degree_semitones(k.scale, degree + 1) - root, fourth = degree_semitones(k.scale, degree + 3) - root;
    // A flat ninth or second grinds against the root, and so does a raised fourth held over it (phrygian's tonic,
    // lydian's fourth): those colors fall back to the plain chord.
    if ((ext == EXT_SUS2 && second == 1) || (ext == EXT_SUS4 && fourth == 6)) ext = EXT_TRIAD;
    if ((ext == EXT_NINTH || ext == EXT_ADD9) && ninth % 12 == 1) ext = ext == EXT_NINTH ? EXT_SEVENTH : EXT_TRIAD;
    Chord c = {.degree = degree, .ext = ext};
    c.tones[c.count++] = 0;
    switch (ext) {
    case EXT_SUS2: c.tones[c.count++] = second; break;
    case EXT_SUS4: c.tones[c.count++] = fourth; break;
    default: c.tones[c.count++] = third; break;
    }
    c.tones[c.count++] = fifth;
    if (ext == EXT_SEVENTH || ext == EXT_NINTH) c.tones[c.count++] = seventh;
    if (ext == EXT_NINTH || ext == EXT_ADD9) c.tones[c.count++] = ninth;
    c.root_pc = mod(k.tonic + root, 12);
    return c;
}

int chord_has_pc(const Chord *c, int pitch_class) {
    for (int i = 0; i < c->count; i++)
        if (mod(c->root_pc + c->tones[i], 12) == mod(pitch_class, 12)) return 1;
    return 0;
}

int chord_nearest(const Chord *c, int note) {
    int best = note, best_d = INT_MAX;
    for (int d = 0; d <= 6; d++)
        for (int sign = -1; sign <= 1; sign += 2) {
            int n = note + d * sign;
            if (chord_has_pc(c, n - 1) && d < best_d) {
                best = n;
                best_d = d;
            }
        }
    return best;
}

int chord_minor(const Chord *c) {
    return c->count > 1 && c->tones[1] == 3;
}

void chord_voice(const Chord *c, int voices, int low, int high, const int *previous, int *out) {
    // Candidate notes: every chord tone in range. Pick, for each voice, the closest unused tone to where it was.
    int pool[64], n = 0;
    for (int note = low; note <= high && n < 64; note++)
        if (chord_has_pc(c, note - 1)) pool[n++] = note;
    int used[64] = {0};
    for (int v = 0; v < voices; v++) {
        int target = previous ? previous[v] : low + (high - low) * (v + 1) / (voices + 1);
        int best = -1, best_d = INT_MAX;
        for (int i = 0; i < n; i++) {
            if (used[i]) continue;
            int d = abs(pool[i] - target);
            // Prefer tones the chord doesn't have yet, so the voicing stays complete.
            int dup = 0;
            for (int j = 0; j < n; j++)
                if (used[j] && (pool[j] - pool[i]) % 12 == 0) dup = 1;
            d += dup ? 7 : 0;
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        if (best < 0) {
            out[v] = target;
            continue;
        }
        used[best] = 1;
        out[v] = pool[best];
    }
    // Keep voices ordered low to high so pads never cross.
    for (int i = 1; i < voices; i++)
        for (int j = i; j > 0 && out[j] < out[j - 1]; j--) {
            int t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
}

// Nearest scale note to a pitch, for snapping melodic steps.
static int key_snap(Key k, int note) {
    for (int d = 0; d < 3; d++) {
        if (key_contains(k, note - 1 - d)) return note - d;
        if (key_contains(k, note - 1 + d)) return note + d;
    }
    return note;
}

int key_step(Key k, int note, int steps) {
    note = key_snap(k, note);
    while (steps > 0) {
        do note++;
        while (!key_contains(k, note - 1));
        steps--;
    }
    while (steps < 0) {
        do note--;
        while (!key_contains(k, note - 1));
        steps++;
    }
    return note;
}
