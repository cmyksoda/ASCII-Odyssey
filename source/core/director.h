// SPDX-License-Identifier: GPL-3.0-only
#ifndef DIRECTOR_H
#define DIRECTOR_H

#include "scene.h"

#define MAX_SHOTS 48

typedef enum { ENTER_CUT, ENTER_DISSOLVE, ENTER_WIPE, ENTER_RADIAL, ENTER_DECODE } Entrance;

typedef struct {
    int kind;
    float start, end, fade;
    Entrance entrance;
    _Alignas(8) unsigned char state[SCENE_STATE];
} Shot;

typedef struct {
    Shot shots[MAX_SHOTS];
    int count;
} Direction;

// Casts a handful of scenes for the piece and gives each section its shots.
void direction_plan(Direction *d, const Score *score, Rng *r, const Frame *proto);
// Draws the shot playing at f->t, blended with the one before it while it makes its entrance.
void direction_draw(Direction *d, Frame *f, Grid *scratch);

#endif
