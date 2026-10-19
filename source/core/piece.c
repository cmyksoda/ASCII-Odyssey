// SPDX-License-Identifier: GPL-3.0-only
#include "piece.h"

#include "mathx.h"

#include <string.h>

static void layout_title(Piece *p) {
    static const FigletId fonts[] = {FIGLET_BIG, FIGLET_STANDARD, FIGLET_SLANT, FIGLET_SMALL};
    // FIGlet's own space is a sliver; three of them keep the words apart behind the decoding scramble.
    char upper[SEED_MAX * 3 + 1];
    int n = 0;
    for (const char *s = p->comp.info.title; *s; s++) {
        char c = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
        for (int k = 0; k < (c == ' ' ? 3 : 1); k++) upper[n++] = c;
    }
    upper[n] = 0;
    for (unsigned i = 0; i < sizeof fonts / sizeof fonts[0]; i++) {
        const FigletFont *font = figlet_font(fonts[i]);
        if (!font) continue;
        int w = figlet_render(font, upper, p->art, PIECE_ART_W);
        if (w > 0 && w <= p->grid.cols - 4) {
            p->art_font = font;
            p->art_w = w;
            p->art_h = font->height;
            while (p->art_h > 1) {
                int blank = 1;
                for (int x = 0; x < w; x++) blank &= p->art[(p->art_h - 1) * PIECE_ART_W + x] == ' ';
                if (!blank) break;
                p->art_h--;
            }
            return;
        }
    }
    p->art_font = NULL;
}

int piece_create(Piece *p, const char *seed, Mood mood, LengthPref length, const Viewport *vp) {
    memset(p, 0, sizeof *p);
    uint64_t hash = seed_hash(seed);
    if (compose(hash, seed, mood, length, &p->comp) != 0) return -1;
    if (score_build(&p->score, &p->comp) != 0) return -1;
    Rng r;
    rng_init(&r, hash, "visuals");
    p->palette = palette_pick(&r, p->comp.info.mood);
    int chunky = p->comp.info.mood == MOOD_KEYGEN && rng_chance(&r, 50);
    int design = chunky ? 8 : 6, height = chunky ? 16 : 12;
    if (grid_init(&p->grid, vp, design, height) != 0 || grid_init(&p->scratch, vp, design, height) != 0) return -1;
    if (canvas_init(&p->canvas, &p->grid) != 0 || stroker_init(&p->stroker, &p->grid) != 0) return -1;
    AsciiStyle style = ascii_default;
    style.correlate = 1;
    style.flat = 0.12f;
    ascii_prepare(&p->matcher, p->grid.face, &style);
    Frame proto = {.score = &p->score, .palette = p->palette, .canvas = &p->canvas, .grid = &p->grid,
                   .stroker = &p->stroker, .matcher = &p->matcher, .fx = &p->fx};
    direction_plan(&p->direction, &p->score, &r, &proto);
    layout_title(p);
    return 0;
}

void piece_free(Piece *p) {
    composition_free(&p->comp);
    score_free(&p->score);
    grid_free(&p->grid);
    grid_free(&p->scratch);
    canvas_free(&p->canvas);
    stroker_free(&p->stroker);
}

double piece_length(const Piece *p) {
    return p->score.length;
}

// The piece's name, decoded out of noise over the intro, held, then dissolved: the only words a piece shows.
static void title_card(Piece *p, float t) {
    const float in = 0.8f, hold = 5.5f, out = 7.0f;
    if (!p->art_font || t < in || t > out) return;
    Grid *g = &p->grid;
    int col0 = (g->cols - p->art_w) / 2, row0 = (g->rows - p->art_h) / 2;
    float reveal = smoothstep(in, in + 1.6f, t), vanish = smoothstep(hold, out, t);
    Rgbf pearl = palette_ramp(p->palette, 1.0f), gold = palette_accent(p->palette, 1);
    for (int y = -1; y <= p->art_h; y++)
        for (int x = -2; x < p->art_w + 2; x++) {
            int col = col0 + x, row = row0 + y;
            if (col < 0 || row < 0 || col >= g->cols || row >= g->rows) continue;
            int inside = y >= 0 && y < p->art_h && x >= 0 && x < p->art_w;
            char ch = inside ? p->art[y * PIECE_ART_W + x] : ' ';
            float h = hash01((uint32_t)(row * 977 + col * 131));
            if (h > reveal || h < vanish) continue;
            Cell *cell = grid_at(g, col, row);
            if (ch == ' ') {
                cell->glyph = ' ';
                continue;
            }
            Rgbf color = rgbf_mix(pearl, gold, (float)y / (float)(p->art_h > 1 ? p->art_h - 1 : 1));
            int settling = h > reveal - 0.1f;
            uint32_t roll = hash_u32((uint32_t)(row * 31 + col) ^ (uint32_t)(t * 20));
            *cell = (Cell){(uint8_t)(settling ? "*+:.=#"[roll % 6] : ch), rgb_from_f(color)};
        }
}

void piece_draw(Piece *p, double t, Screen *out, int calm, int still, const float *scope, int scope_frames) {
    fx_reset(&p->fx, p->palette->bg);
    Frame f = {.t = (float)t, .energy = score_energy(&p->score, (float)t), .score = &p->score, .palette = p->palette,
               .canvas = &p->canvas, .grid = &p->grid, .stroker = &p->stroker, .matcher = &p->matcher, .fx = &p->fx,
               .calm = calm, .still = still, .scope = scope, .scope_frames = scope_frames};
    direction_draw(&p->direction, &f, &p->scratch);
    title_card(p, (float)t);
    if (p->palette->light) p->fx.glow = 0;
    // Fade in from black at the start and out into the tail at the end.
    float end = (float)p->comp.info.length;
    p->fx.gain = smoothstep(0.0f, 1.2f, (float)t) * (1 - smoothstep(end + 1.0f, end + 5.0f, (float)t));
    out->scene = &p->grid;
    out->ui = NULL;
    out->fx = p->fx;
}
