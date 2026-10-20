// SPDX-License-Identifier: GPL-3.0-only
#ifndef FIGLET_H
#define FIGLET_H

#include "grid.h"

#define FIGLET_MAX_HEIGHT 12
#define FIGLET_GLYPHS 95
#define FIGLET_LINES 3

typedef struct {
    char hardblank;
    int height, baseline;
    int layout;
    const char *rows[FIGLET_GLYPHS][FIGLET_MAX_HEIGHT];
    unsigned char widths[FIGLET_GLYPHS];
    char *data;
} FigletFont;

// Every face and the name its font file is embedded under (a hyphen becomes an underscore): FIGlet's own fonts,
// Odyssey's, then the ones from FIGlet's contributed collection, credited in licenses/FIGlet-contributed-fonts.txt.
#define FIGLET_FACES(X)                                                                                                \
    X(STANDARD, standard)                                                                                              \
    X(SLANT, slant)                                                                                                    \
    X(SMALL, small)                                                                                                    \
    X(BIG, big)                                                                                                        \
    X(BLOCK, block)                                                                                                    \
    X(LEAN, lean)                                                                                                      \
    X(SCRIPT, script)                                                                                                  \
    X(SHADOW, shadow)                                                                                                  \
    X(BANNER, banner)                                                                                                  \
    X(BUBBLE, bubble)                                                                                                  \
    X(DIGITAL, digital)                                                                                                \
    X(PIXEL, pixel)                                                                                                    \
    X(DOTS, dots)                                                                                                      \
    X(WIRE, wire)                                                                                                      \
    X(SCANLINE, scanline)                                                                                              \
    X(NEON, neon)                                                                                                      \
    X(THREE_D, 3_d)                                                                                                    \
    X(AVATAR, avatar)                                                                                                  \
    X(BASIC, basic)                                                                                                    \
    X(BULBHEAD, bulbhead)                                                                                              \
    X(COLOSSAL, colossal)                                                                                              \
    X(COMPUTER, computer)                                                                                              \
    X(CRICKET, cricket)                                                                                                \
    X(DIAMOND, diamond)                                                                                                \
    X(DOOM, doom)                                                                                                      \
    X(DOTMATRIX, dotmatrix)                                                                                            \
    X(DOUBLE, double)                                                                                                  \
    X(EPIC, epic)                                                                                                      \
    X(FENDER, fender)                                                                                                  \
    X(FUZZY, fuzzy)                                                                                                    \
    X(GRACEFUL, graceful)                                                                                              \
    X(GRAFFITI, graffiti)                                                                                              \
    X(JAZMINE, jazmine)                                                                                                \
    X(KBAN, kban)                                                                                                      \
    X(LARRY3D, larry3d)                                                                                                \
    X(NANCYJ, nancyj)                                                                                                  \
    X(NANCYJ_FANCY, nancyj_fancy)                                                                                      \
    X(OGRE, ogre)                                                                                                      \
    X(PEBBLES, pebbles)                                                                                                \
    X(POISON, poison)                                                                                                  \
    X(PUFFY, puffy)                                                                                                    \
    X(RECTANGLES, rectangles)                                                                                          \
    X(REV, rev)                                                                                                        \
    X(ROMAN, roman)                                                                                                    \
    X(ROUNDED, rounded)                                                                                                \
    X(SPEED, speed)                                                                                                    \
    X(STARWARS, starwars)                                                                                              \
    X(STELLAR, stellar)                                                                                                \
    X(TINKER_TOY, tinker_toy)                                                                                          \
    X(UNIVERS, univers)

typedef enum {
#define FIGLET_ID(id, file) FIGLET_##id,
    FIGLET_FACES(FIGLET_ID)
#undef FIGLET_ID
    FIGLET_FONTS
} FigletId;

// Where each line of a wrapped title sits in its block: the line's first row, its width, and its height with blank
// bottom rows trimmed.
typedef struct {
    int lines, width, height;
    int top[FIGLET_LINES], wide[FIGLET_LINES], tall[FIGLET_LINES];
} FigletLayout;

const FigletFont *figlet_font(FigletId id);
const char *figlet_name(FigletId id);
// Lays text out on one line, as the figlet command does, without wrapping. out holds height rows of stride max_w;
// returns the width used, or -1 if it didn't fit.
int figlet_render(const FigletFont *f, const char *text, char *out, int max_w);
// Lays text out over up to FIGLET_LINES lines a row apart into max_h rows of out: words fill each line while it stays
// within max_w, and with split set, a word too wide for a line of its own is broken across lines. Returns 0, or -1.
int figlet_wrap(const FigletFont *f, const char *text, int max_w, int max_h, int split, char *out, int stride,
                FigletLayout *layout);

#endif
