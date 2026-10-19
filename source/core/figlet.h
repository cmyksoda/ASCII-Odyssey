// SPDX-License-Identifier: GPL-3.0-only
#ifndef FIGLET_H
#define FIGLET_H

#include "grid.h"

#define FIGLET_MAX_HEIGHT 12
#define FIGLET_GLYPHS 95

typedef struct {
    char hardblank;
    int height, baseline;
    int layout;
    const char *rows[FIGLET_GLYPHS][FIGLET_MAX_HEIGHT];
    unsigned char widths[FIGLET_GLYPHS];
    char *data;
} FigletFont;

typedef enum { FIGLET_STANDARD, FIGLET_SLANT, FIGLET_SMALL, FIGLET_BIG, FIGLET_FONTS } FigletId;

int figlet_load(FigletFont *f, const unsigned char *flf, unsigned int size);
const FigletFont *figlet_font(FigletId id);
// Lays text out like the figlet command with no width limit. out holds height rows of stride
// max_w; returns the width used, or -1 if it didn't fit.
int figlet_render(const FigletFont *f, const char *text, char *out, int max_w);
int figlet_width(const FigletFont *f, const char *text);

#endif
