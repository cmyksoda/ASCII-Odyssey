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
    const Palette *palettes[PALETTE_FAMILY];
    // The backup way: reactivity from a per-section plan instead of each scene's own nature.
    int by_section;
} Direction;

// Gives each section shots of eight to ten seconds, each a scene whose intensity suits the music there, picked by
// how much the mood likes it, with entrances that land on the bar line.
void direction_plan(Direction *d, const Score *score, Rng *r, const Frame *proto,
                    const Palette *const palettes[PALETTE_FAMILY]);
// Draws the shot playing at f->t, blended with the one before it while it makes its entrance.
void direction_draw(Direction *d, Frame *f, Grid *scratch);

#endif
