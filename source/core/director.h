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
    ENTER_PUSH,
    ENTER_DOORS,
    ENTER_SHATTER,
    ENTER_TYPEWRITER,
    ENTER_SWIRL,
    ENTER_ZOOM,
    ENTER_INTERLACE,
    ENTER_MOSAIC,
    ENTER_SLIDE,
    ENTER_CASCADE,
    ENTER_GLITCH,
    ENTER_FLIP,
    ENTER_STAR,
    ENTER_HEART,
    ENTER_KALEIDO,
    ENTER_TETRIS,
    ENTER_RIPPLE,
    ENTER_HALFTONE,
    ENTER_PINWHEEL,
    ENTER_HONEYCOMB,
    ENTER_CRT,
    ENTRANCES
} Entrance;

// One name for each Entrance, in order.
extern const char *const *const entrance_names;

typedef struct {
    int kind;
    // Which of the piece's palettes the shot paints with, and which way a wipe travels (0 right, 1 left, 2 down, 3 up).
    int palette, direction;
    float start, end, fade;
    // The intensity the director aimed for: the section's energy, scaled by how hard the piece drives.
    float target;
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
