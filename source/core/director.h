// SPDX-License-Identifier: GPL-3.0-only
#ifndef DIRECTOR_H
#define DIRECTOR_H

#include "scene.h"

#define MAX_SHOTS 64

typedef enum {
    ENTER_CUT,
    ENTER_DISSOLVE,
    ENTER_WIPE,
    ENTER_DIAGONAL,
    ENTER_RADIAL,
    ENTER_IRIS,
    ENTER_BLINDS,
    ENTER_CHECKER,
    ENTER_SPIRAL,
    ENTER_MELT,
    ENTER_DECODE,
    ENTER_FADE,
    ENTRANCES
} Entrance;

extern const char *const entrance_names[ENTRANCES];

typedef struct {
    int kind;
    // Which of the piece's palettes the shot paints with, and which way a wipe travels (0 right, 1 left, 2 down, 3 up).
    int palette, direction;
    float start, end, fade;
    Entrance entrance;
    _Alignas(8) unsigned char state[SCENE_STATE];
} Shot;

typedef struct {
    Shot shots[MAX_SHOTS];
    int count;
    const Palette *palettes[3];
    // The backup way: reactivity from a per-section plan instead of each scene's own nature.
    int by_section;
    // Which shot the scratch grid holds, when it was drawn and the glow it asked for, so an outgoing shot redraws at
    // half rate without its glow flickering in between.
    int scratch_shot;
    float scratch_t, scratch_glow;
} Direction;

// Gives each section shots a few bars long, never repeating a scene while unused ones remain, and matching each
// scene's intensity to the music's energy.
void direction_plan(Direction *d, const Score *score, Rng *r, const Frame *proto, const Palette *const palettes[3]);
// Draws the shot playing at f->t, blended with the one before it while it makes its entrance.
void direction_draw(Direction *d, Frame *f, Grid *scratch);

#endif
