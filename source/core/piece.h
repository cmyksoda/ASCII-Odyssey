// SPDX-License-Identifier: GPL-3.0-only
#ifndef PIECE_H
#define PIECE_H

#include "director.h"
#include "figlet.h"
#include "screen.h"

#define PIECE_ART_W 200
#define PIECE_ART_H (FIGLET_LINES * (FIGLET_MAX_HEIGHT + 1))

// One voyage: the song, its score, the shots that picture it, and the grids they draw into.
typedef struct {
    Composition comp;
    Score score;
    Direction direction;
    Grid grid, scratch;
    Canvas canvas;
    Stroker stroker;
    AsciiMatcher matcher;
    FrameFx fx;
    // The piece's palette family (calm parts, drops, builds and breaks), and the first of them, for its title.
    Palette colors[PALETTE_FAMILY];
    const Palette *palette;
    const FigletFont *art_font;
    char art[PIECE_ART_H * PIECE_ART_W];
    FigletLayout art_layout;
    int intro;
    // The backup reactivity plan, per section, for when the direction is set to follow it instead of each scene's own
    // nature: all the way through, at the big moments, swelling, or in pockets.
    float react[MAX_SECTIONS];
    int react_shape;
} Piece;

int piece_create(Piece *p, const char *seed, Mood mood, LengthPref length, const Viewport *vp);
void piece_free(Piece *p);
// The picture at t seconds into the piece.
void piece_draw(Piece *p, double t, Screen *out, int calm, int still, const float *scope, int scope_frames);
double piece_length(const Piece *p);
// Recasts every shot as the named scene, for looking at one scene against real music; -1 when there's no such scene.
int piece_force_scene(Piece *p, const char *name);
// Paints every shot with the named hand-made palette; -1 when there's no such palette.
int piece_force_palette(Piece *p, const char *name);
// Writes the title in the named face; -1 when there's no such face or the title can't fit it.
int piece_force_face(Piece *p, const char *name);

#endif
