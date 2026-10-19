// SPDX-License-Identifier: GPL-3.0-only
#ifndef PIECE_H
#define PIECE_H

#include "director.h"
#include "figlet.h"
#include "screen.h"

#define PIECE_ART_W 200

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
    const Palette *palette;
    const FigletFont *art_font;
    char art[FIGLET_MAX_HEIGHT * PIECE_ART_W];
    int art_w, art_h;
    // How strongly each section's picture answers the music.
    float react[MAX_SECTIONS];
} Piece;

int piece_create(Piece *p, const char *seed, Mood mood, LengthPref length, const Viewport *vp);
void piece_free(Piece *p);
// The picture at t seconds into the piece.
void piece_draw(Piece *p, double t, Screen *out, int calm, int still, const float *scope, int scope_frames);
double piece_length(const Piece *p);

#endif
