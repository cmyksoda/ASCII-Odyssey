// SPDX-License-Identifier: GPL-3.0-only
// The rainbow palette puts every hue on screen at once, on black, even in a scene of only two brightnesses, where a
// ramp by brightness would show two colors.
#include "core/app.h"
#include "core/piece.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                                                                     \
    do {                                                                                                                \
        if (!(cond)) {                                                                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                      \
            failures++;                                                                                                 \
        }                                                                                                               \
    } while (0)

// Which sixth of the color wheel a color sits in, from red round to magenta, or -1 for a gray.
static int sector(Rgb c) {
    int hi = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    int lo = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
    if (hi - lo < 24) return -1;
    float d = (float)(hi - lo), h = hi == c.r   ? (float)(c.g - c.b) / d
                                    : hi == c.g ? 2 + (float)(c.b - c.r) / d
                                                : 4 + (float)(c.r - c.g) / d;
    return (int)(h < 0 ? h + 6 : h) % 6;
}

static void rainbow_shows_every_hue_on_black(void) {
    static const char *const NAMES[6] = {"red", "yellow", "green", "cyan", "blue", "magenta"};
    Viewport vp;
    viewport_init(&vp, 0, 0);
    Piece p;
    CHECK(piece_create(&p, "violet lagoon", MOOD_ANY, LENGTH_MEDIUM, &vp) == 0);
    CHECK(piece_force_palette(&p, "rainbow") == 0);
    CHECK(piece_force_scene(&p, "checker") == 0);
    int seen[6] = {0};
    // Several moments, so one that happens to be dark can't decide it either way.
    for (float t = 20; t <= 80; t += 20) {
        Screen screen = {0};
        piece_draw(&p, t, &screen, 0, 0, NULL, 0);
        CHECK(screen.fx.bg.r == 0 && screen.fx.bg.g == 0 && screen.fx.bg.b == 0);
        int lit = 0;
        for (int row = 0; row < p.grid.rows; row++)
            for (int col = 0; col < p.grid.cols; col++) {
                Cell *c = grid_at(&p.grid, col, row);
                if (c->glyph == ' ') continue;
                lit++;
                int s = sector(c->fg);
                if (s >= 0) seen[s] = 1;
            }
        CHECK(lit > 500);
    }
    for (int s = 0; s < 6; s++) {
        CHECK(seen[s]);
        if (!seen[s]) printf("  no %s anywhere on screen\n", NAMES[s]);
    }
    piece_free(&p);
}

int main(void) {
    app_boot();
    rainbow_shows_every_hue_on_black();
    if (!failures) printf("palette: all passed\n");
    return failures ? 1 : 0;
}
