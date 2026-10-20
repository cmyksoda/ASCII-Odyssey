// SPDX-License-Identifier: GPL-3.0-only
#include "piece.h"

#include "mathx.h"

#include <string.h>

// Each mood writes its titles in a few faces of its own; one is picked at random, and a title too wide for any of them
// falls back to the plainer, narrower ones.
#define TITLE_CHOICES 5

static const FigletId TITLE_FACES[MOOD_COUNT][TITLE_CHOICES] = {
    [MOOD_KEYGEN] = {FIGLET_BLOCK, FIGLET_BIG, FIGLET_BANNER, FIGLET_DIGITAL, FIGLET_PIXEL},
    [MOOD_AMBIENT] = {FIGLET_SCRIPT, FIGLET_LEAN, FIGLET_SLANT, FIGLET_SMSCRIPT, FIGLET_SMSLANT},
    [MOOD_SYNTHWAVE] = {FIGLET_LEAN, FIGLET_SLANT, FIGLET_SHADOW, FIGLET_DOTS, FIGLET_SMSLANT},
    [MOOD_LOFI] = {FIGLET_SCRIPT, FIGLET_SHADOW, FIGLET_STANDARD, FIGLET_SMSHADOW, FIGLET_BUBBLE},
    [MOOD_TRANCE] = {FIGLET_SLANT, FIGLET_LEAN, FIGLET_BIG, FIGLET_SMSLANT, FIGLET_DOTS},
    [MOOD_JUNGLE] = {FIGLET_BIG, FIGLET_BLOCK, FIGLET_SHADOW, FIGLET_PIXEL, FIGLET_DIGITAL},
    [MOOD_MUSICBOX] = {FIGLET_SCRIPT, FIGLET_STANDARD, FIGLET_LEAN, FIGLET_BUBBLE, FIGLET_SMSCRIPT},
    [MOOD_DUB] = {FIGLET_SHADOW, FIGLET_BLOCK, FIGLET_BIG, FIGLET_SMSHADOW, FIGLET_DOTS},
    [MOOD_DUBSTEP] = {FIGLET_BLOCK, FIGLET_BANNER, FIGLET_SHADOW, FIGLET_PIXEL, FIGLET_DIGITAL}};

static void layout_title(Piece *p, Rng *r) {
    const FigletId *own = TITLE_FACES[p->comp.info.mood];
    int first = rng_range(r, 0, TITLE_CHOICES);
    // The mood's own faces in turn from a random one, then plain ones narrow enough for almost any title.
    FigletId fonts[TITLE_CHOICES + 3];
    for (int i = 0; i < TITLE_CHOICES; i++) fonts[i] = own[(first + i) % TITLE_CHOICES];
    fonts[TITLE_CHOICES] = FIGLET_STANDARD, fonts[TITLE_CHOICES + 1] = FIGLET_SLANT, fonts[TITLE_CHOICES + 2] = FIGLET_SMALL;
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

// Each seed decides how much of its piece answers the music, and where: all the way through, at the big moments,
// swelling toward the end, or in pockets here and there.
static void plan_reactivity(Piece *p, Rng *r) {
    const SongInfo *info = &p->comp.info;
    int shape = p->react_shape = rng_range(r, 0, 4);
    float amount = rng_between(r, 0.55f, 1.0f);
    for (int s = 0; s < info->section_count; s++) {
        const Section *sec = &info->sections[s];
        int big = sec->kind == SECTION_DROP || sec->kind == SECTION_BUILD;
        float level;
        switch (shape) {
        case 0: level = 0.6f + 0.4f * sec->energy; break;
        case 1: level = big ? 1.0f : sec->kind == SECTION_VERSE ? 0.35f : 0.08f; break;
        case 2: level = (float)(s + 1) / (float)info->section_count; break;
        default: level = rng_chance(r, 55) ? rng_between(r, 0.6f, 1.0f) : rng_between(r, 0.0f, 0.25f); break;
        }
        p->react[s] = level * amount;
    }
}

// Reactivity eases from one section's level to the next over a bar, so nothing lurches at a section line.
static float reactivity_at(const Piece *p, float t) {
    const SongInfo *info = &p->comp.info;
    int s = 0;
    while (s + 1 < info->section_count && t >= (float)info->sections[s + 1].start) s++;
    float into = t - (float)info->sections[s].start, bar = (float)info->bar_seconds;
    if (s == 0 || into >= bar) return p->react[s];
    return mixf(p->react[s - 1], p->react[s], smoothstep(0, bar, into));
}

int piece_create(Piece *p, const char *seed, Mood mood, LengthPref length, const Viewport *vp) {
    memset(p, 0, sizeof *p);
    uint64_t hash = seed_hash(seed);
    if (compose(hash, seed, mood, length, &p->comp) != 0) return -1;
    if (score_build(&p->score, &p->comp) != 0) return -1;
    Rng r;
    rng_init(&r, hash, "visuals");
    palette_family(&r, p->comp.info.mood, p->colors);
    p->palette = &p->colors[0];
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
    const Palette *family[PALETTE_FAMILY] = {&p->colors[0], &p->colors[1], &p->colors[2], &p->colors[3]};
    direction_plan(&p->direction, &p->score, &r, &proto, family);
    plan_reactivity(p, &r);
    layout_title(p, &r);
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

int piece_force_scene(Piece *p, const char *name) {
    int kind = -1;
    for (int k = 0; k < scene_kind_count; k++)
        if (!strcmp(scene_kinds[k].name, name)) kind = k;
    if (kind < 0) return -1;
    Rng r;
    rng_init(&r, seed_hash(p->comp.info.title), "forced");
    Frame proto = {.score = &p->score, .palette = p->palette, .canvas = &p->canvas, .grid = &p->grid,
                   .stroker = &p->stroker, .matcher = &p->matcher, .fx = &p->fx};
    for (int i = 0; i < p->direction.count; i++) {
        p->direction.shots[i].kind = kind;
        scene_kinds[kind].setup(p->direction.shots[i].state, &r, &proto);
    }
    return 0;
}

int piece_force_palette(Piece *p, const char *name) {
    for (int i = 0; i < palette_curated_count(); i++) {
        Palette pal;
        palette_curated(i, &pal);
        if (strcmp(pal.name, name)) continue;
        for (int k = 0; k < PALETTE_FAMILY; k++) p->colors[k] = pal;
        return 0;
    }
    return -1;
}

double piece_length(const Piece *p) {
    return p->score.length;
}

// The piece's name, decoded out of noise over the intro, held, then dissolved: the only words a piece shows.
static void title_card(Piece *p, float t) {
    const float in = 0.8f, hold = 5.5f, out = 7.0f;
    if (!p->art_font || t < in || t > out) return;
    Grid *g = &p->grid;
    int nudge, col0 = grid_center(g, p->art_w, &nudge), row0 = (g->rows - p->art_h) / 2;
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
            *cell = (Cell){(uint8_t)(settling ? "*+:.=#"[roll % 6] : ch), rgb_from_f(color), (uint8_t)nudge};
        }
}

void piece_draw(Piece *p, double t, Screen *out, int calm, int still, const float *scope, int scope_frames) {
    fx_reset(&p->fx, p->palette->bg);
    Frame f = {.t = (float)t, .energy = score_energy(&p->score, (float)t), .section_react = reactivity_at(p, (float)t),
               .score = &p->score, .palette = p->palette, .canvas = &p->canvas, .grid = &p->grid, .stroker = &p->stroker,
               .matcher = &p->matcher, .fx = &p->fx, .calm = calm, .still = still, .scope = scope, .scope_frames = scope_frames};
    direction_draw(&p->direction, &f, &p->scratch);
    title_card(p, (float)t);
    // A reactive scene's whole picture also breathes with the beat: a little glow on the kick and a nudge of motion.
    // The scenes themselves carry the real answer; this only ties them to the frame.
    float kick = score_pulse(&p->score, ROLE_KICK, (float)t, 0.16f);
    float answer = f.react * (calm ? 0.3f : 1.0f);
    p->fx.glow += answer * 0.25f * kick;
    if (!still) p->fx.shake_y += answer * kick * 1.0f;
    if (f.palette->light) p->fx.glow = 0;
    // Fade in from black at the start and out into the tail at the end.
    float end = (float)p->comp.info.length;
    p->fx.gain = smoothstep(0.0f, 1.2f, (float)t) * (1 - smoothstep(end + 1.0f, end + 5.0f, (float)t)) * (1 + answer * 0.1f * kick);
    out->scene = &p->grid;
    out->ui = NULL;
    out->fx = p->fx;
}
