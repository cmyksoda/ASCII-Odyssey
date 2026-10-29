// SPDX-License-Identifier: GPL-3.0-or-later
// Once a title has arrived, every letter of it shows while it's held, whichever way it came in and whatever face it's
// in.
#include "check.h"
#include "core/app.h"
#include "core/piece.h"

#include <stdio.h>

static int cells_missing(Piece *p, float t) {
    Screen screen = {0};
    piece_draw(p, t, &screen, 0, 0, NULL, 0);
    const FigletLayout *lay = &p->art_layout;
    int row0 = (p->grid.rows - lay->height) / 2, missing = 0;
    for (int l = 0; l < lay->lines; l++) {
        int nudge, col0 = grid_center(&p->grid, lay->wide[l], &nudge);
        for (int y = 0; y < lay->tall[l]; y++)
            for (int x = 0; x < lay->wide[l]; x++) {
                char ch = p->art[(lay->top[l] + y) * PIECE_ART_W + x];
                missing += ch != ' ' && grid_at(&p->grid, col0 + x, row0 + lay->top[l] + y)->glyph != (uint8_t)ch;
            }
    }
    return missing;
}

// Digital gives a letter one cell, so one cell left arriving is a whole letter gone, like static rocket's last T.
static void every_letter_shows_while_held(void) {
    static const struct {
        const char *seed, *face;
        int wide, lines;
    } TITLES[] = {
        {"static rocket", "digital", 0, 1},
        {"rain orchard", "neon", 0, 1},
        {"violet lagoon", "colossal", 0, 2},
        {"satellite comet", "univers", 1, 2},
    };
    for (size_t i = 0; i < sizeof TITLES / sizeof TITLES[0]; i++) {
        Viewport vp;
        viewport_init(&vp, TITLES[i].wide, 0);
        Piece p;
        CHECK(piece_create(&p, TITLES[i].seed, MOOD_ANY, LENGTH_SHORT, &vp) == 0);
        int forced = piece_force_face(&p, TITLES[i].face) == 0;
        CHECK(forced);
        CHECK(p.art_layout.lines == TITLES[i].lines);
        for (int intro = 0; forced && intro < INTROS; intro++) {
            p.intro = intro;
            for (float t = 2.5f; t < 5.5f; t += 1.45f) {
                int missing = cells_missing(&p, t);
                CHECK(!missing);
                if (missing)
                    printf("  %s in %s, intro %d, %.2f s: %d cells don't show their letter\n", TITLES[i].seed,
                           TITLES[i].face, intro, t, missing);
            }
        }
        piece_free(&p);
    }
}

int main(void) {
    app_boot();
    every_letter_shows_while_held();
    if (!failures) printf("title: all passed\n");
    return failures ? 1 : 0;
}
