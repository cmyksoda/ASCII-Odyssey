// SPDX-License-Identifier: GPL-3.0-only
#ifndef TITLE_H
#define TITLE_H

#include "ascii.h"
#include "figlet.h"
#include "screen.h"
#include "stroke.h"

#define TITLE_ART_W 160

typedef struct {
    Canvas canvas;
    AsciiMatcher matcher;
    Stroker stroker;
    const FigletFont *font;
    char art[FIGLET_MAX_HEIGHT * TITLE_ART_W];
    // Where the FIGlet title lands, in scene cells.
    int title_row, title_rows, title_col, title_w, title_nudge;
} Title;

int title_init(Title *t, Grid *scene);
void title_free(Title *t);
// The space backdrop, and the FIGlet title when logo is set, as a pure function of time.
void title_draw(Title *t, double time, Screen *s, int logo);

#endif
