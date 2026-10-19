// SPDX-License-Identifier: GPL-3.0-only
#include "title.h"

#include "figlet.h"
#include "mathx.h"
#include "noise.h"
#include "prof.h"
#include "rng.h"
#include "stroke.h"

#include <string.h>

#define TITLE_TEXT "ASCII Odyssey"

static const Rgb SPACE = {6, 7, 16};
static const Rgb STAR_COOL = {196, 206, 255}, STAR_WARM = {255, 226, 196};
static const Rgb NEBULA_A = {104, 72, 200}, NEBULA_B = {56, 170, 196};
static const Rgb PLANET_DARK = {70, 32, 72}, PLANET_MID = {200, 96, 122}, PLANET_LIGHT = {255, 206, 160};
static const Rgb RING = {232, 214, 176};
static const Rgb PEARL = {255, 240, 222}, GOLD = {255, 204, 120};
static const Rgb COMET_HEAD = {220, 244, 255}, COMET_TAIL = {120, 110, 230};

static void title_layout(Title *t, const Grid *g);

int title_init(Title *t, Grid *scene) {
    memset(t, 0, sizeof *t);
    if (canvas_init(&t->canvas, scene) != 0 || stroker_init(&t->stroker, scene) != 0) return -1;
    AsciiStyle style = ascii_default;
    style.correlate = 1;
    style.flat = 0.12f;
    ascii_prepare(&t->matcher, scene->face, &style);
    title_layout(t, scene);
    return 0;
}

void title_free(Title *t) {
    canvas_free(&t->canvas);
    stroker_free(&t->stroker);
}

// Star dust drawn straight into the grid, with its own speckled ramp and a per-cell jitter
// so neighboring cells of equal density don't line up into stripes.
static void nebula(Grid *g, float aspect, float time, float (*quiet)(const Grid *, int, int, const void *),
                   const void *context) {
    static const char dust[] = " .`,:;'+*";
    const int levels = (int)sizeof dust - 1;
    for (int row = 0; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            float x = ((float)col + 0.5f) / (float)g->cols * 2 * aspect - aspect;
            float y = 1 - ((float)row + 0.5f) / (float)g->rows * 2;
            float warp = noise_fbm(x * 0.6f + 11, y * 0.6f - time * 0.006f) - 0.5f;
            float d = (y - (-0.38f * x + 0.05f + warp * 0.6f)) / 0.48f;
            float band = mexp(-d * d);
            if (band < 0.05f) continue;
            float n = noise_fbm(x * 1.1f + time * 0.012f, y * 1.3f - time * 0.004f);
            float cloud = smoothstep(0.36f, 0.68f, n);
            float density = band * cloud * (1 - quiet(g, col, row, context));
            float jitter = hash01((uint32_t)(row * 977 + col * 131)) - 0.5f;
            int level = (int)(density * (float)levels * 0.95f + jitter * 1.4f);
            if (level <= 0) continue;
            if (level >= levels) level = levels - 1;
            float hue = noise_fbm(x * 0.4f + 4, y * 0.4f + 9);
            Rgb color = rgb_mix(NEBULA_A, NEBULA_B, smoothstep(0.3f, 0.7f, hue));
            color = rgb_scale(color, 0.35f + 0.65f * density);
            Cell *cell = grid_at(g, col, row);
            if (cell->glyph == ' ') *cell = (Cell){(uint8_t)dust[level], color};
        }
}

typedef struct {
    float cx, cy, r, tilt, flat;
} Planet;

static Planet planet_place(float aspect) {
    return (Planet){aspect * 0.58f, -0.48f, 0.3f, -0.26f, 0.3f};
}

static void planet_body(Canvas *c, const Planet *p, float time) {
    const float lx = -0.62f, ly = 0.5f, lz = 0.6f;
    int j0 = (int)((1 - (p->cy + p->r)) / c->sy) - 1, j1 = (int)((1 - (p->cy - p->r)) / c->sy) + 1;
    int i0 = (int)((p->cx - p->r + c->aspect) / c->sx) - 1, i1 = (int)((p->cx + p->r + c->aspect) / c->sx) + 1;
    for (int j = j0 < 0 ? 0 : j0; j <= j1 && j < c->h; j++) {
        float y = canvas_y(c, j) - p->cy;
        for (int i = i0 < 0 ? 0 : i0; i <= i1 && i < c->w; i++) {
            float x = canvas_x(c, i) - p->cx, d2 = x * x + y * y;
            if (d2 >= p->r * p->r) continue;
            float nx = x / p->r, ny = y / p->r, nz = msqrt(1 - nx * nx - ny * ny);
            float lambert = mmax(0, nx * lx + ny * ly + nz * lz);
            float lon = matan2(nx, nz) + time * 0.05f;
            float warp = (noise_fbm(lon * 0.8f + 3, ny * 2.0f) - 0.5f) * 4.0f;
            float band = 0.5f + 0.5f * msin(ny * 11.0f + warp);
            float limb = smoothstep(0.55f, 1.0f, 1 - nz);
            // A dim floor keeps the night side's edge crisp against space; the lit limb glows.
            float ink = 0.26f + 0.46f * mpow(lambert, 0.8f) + 0.3f * limb * lambert + 0.06f * band;
            Rgb surface = rgb_mix(PLANET_MID, PLANET_LIGHT, band * 0.7f);
            Rgb color = rgb_mix(PLANET_DARK, surface, smoothstep(0.0f, 0.65f, lambert));
            color = rgb_mix(color, (Rgb){255, 236, 214}, limb * lambert * 0.6f);
            canvas_max(c, i, j, saturate(ink), color);
        }
    }
}

// Rings as line art: the far half hides behind the planet, the near half crosses in front of it.
static void planet_rings(Stroker *st, const Planet *p) {
    static const float radii[2] = {1.45f, 1.95f}, levels[2] = {1.0f, 0.75f};
    float ct = mcos(p->tilt), sn = msin(p->tilt);
    for (int k = 0; k < 2; k++) {
        StrokePoint run[200];
        int n = 0;
        Rgb color = rgb_scale(RING, levels[k]);
        for (int s = 0; s <= 180; s++) {
            float a = (float)s / 180.0f * TAU_F, radius = p->r * radii[k];
            float u = mcos(a) * radius, v = msin(a) * radius * p->flat;
            float x = p->cx + u * ct - v * sn, y = p->cy + u * sn + v * ct;
            float dx = x - p->cx, dy = y - p->cy;
            int hidden = msin(a) > 0 && dx * dx + dy * dy < p->r * p->r * 1.02f;
            if (!hidden) run[n++] = (StrokePoint){x, y, color};
            if ((hidden || s == 180) && n > 1) stroke_draw(st, run, n, 0);
            if (hidden) n = 0;
        }
    }
}

static int stroke_glyph(float dx, float dy) {
    float a = mabs(matan2(dy, dx));
    if (a > PI_F / 2) a = PI_F - a;
    if (a < 0.4f) return '-';
    if (a > 1.17f) return '|';
    return (dx > 0) == (dy > 0) ? '/' : '\\';
}

static void comet(Grid *g, float time) {
    const float period = 13.0f, life = 2.6f;
    int index = mfloor_i(time / period);
    float age = time - (float)index * period;
    if (age > life) return;
    float h = hash01((uint32_t)index * 977u + 13u);
    float x0 = (float)g->cols * (0.05f + 0.25f * h), y0 = (float)g->rows * (0.04f + 0.04f * h);
    float x1 = (float)g->cols * (0.55f + 0.3f * h), y1 = (float)g->rows * (0.42f + 0.1f * h);
    float t = ease_out(age / life);
    float fade = smoothstep(life, life * 0.55f, age) * smoothstep(0.0f, 0.3f, age);
    // Screen-space direction: cells are twice as tall as they are wide.
    float dx = x1 - x0, dy = -(y1 - y0) * 2;
    int glyph = stroke_glyph(dx, dy);
    float len = mhypot(x1 - x0, y1 - y0);
    for (int s = 14; s >= 0; s--) {
        float back = (float)s * 1.1f / len, u = t - back;
        if (u < 0) continue;
        float k = 1 - (float)s / 15.0f;
        Rgb color = rgb_scale(rgb_mix(COMET_TAIL, COMET_HEAD, k), fade * (0.25f + 0.75f * k * k));
        int ch = s == 0 ? '*' : glyph;
        Cell *cell = grid_at(g, clampi((int)mixf(x0, x1, u), 0, g->cols - 1), clampi((int)mixf(y0, y1, u), 0, g->rows - 1));
        if (cell->glyph == ' ' || s == 0) *cell = (Cell){(uint8_t)ch, color};
    }
}

static int faint(const Cell *cell) {
    return cell->glyph == ' ' || cell->glyph == '.' || cell->glyph == ':';
}

static void stars(Grid *g, float time) {
    static const float speed[3] = {0.12f, 0.3f, 0.7f}, bright[3] = {0.42f, 0.72f, 1.0f};
    static const int density[3] = {16, 55, 170};
    for (int layer = 0; layer < 3; layer++) {
        int count = g->cols * g->rows / density[layer];
        for (int s = 0; s < count; s++) {
            uint32_t id = (uint32_t)(layer * 100003 + s * 2);
            float hx = hash01(id), hy = hash01(id + 1), hp = hash01(id + 7);
            float fx = hx * (float)g->cols - time * speed[layer];
            fx -= mfloor(fx / (float)g->cols) * (float)g->cols;
            int col = (int)fx, row = (int)(hy * (float)g->rows);
            float twinkle = 0.55f + 0.45f * msin(time * (1.1f + 3.0f * hp) + hp * TAU_F);
            float level = bright[layer] * twinkle;
            int glyph = layer == 0 ? '.' : layer == 1 ? (twinkle > 0.82f ? '+' : '.') : (twinkle > 0.8f ? '*' : '+');
            Rgb color = rgb_scale(rgb_mix(STAR_COOL, STAR_WARM, hash01(id + 3)), level);
            Cell *cell = grid_at(g, col, row);
            if (faint(cell)) *cell = (Cell){(uint8_t)glyph, color};
        }
    }
}

static void clear_box(Grid *g, int col, int row, int w, int h) {
    for (int y = row; y < row + h; y++)
        for (int x = col; x < col + w; x++)
            if (x >= 0 && y >= 0 && x < g->cols && y < g->rows) grid_at(g, x, y)->glyph = ' ';
}

typedef struct {
    int col, row, w, h;
} Box;

// How much to hush the background near text: 1 inside a padded box, fading out over a few cells.
static float hush(const Grid *g, int col, int row, const void *context) {
    const Box *boxes = context;
    float q = 0;
    (void)g;
    for (int i = 0; i < 2; i++) {
        const Box *b = &boxes[i];
        float dx = mmax(0, mmax((float)(b->col - col), (float)(col - (b->col + b->w - 1))));
        float dy = mmax(0, mmax((float)(b->row - row), (float)(row - (b->row + b->h - 1)))) * 2;
        q = mmax(q, 1 - smoothstep(0.0f, 5.0f, mhypot(dx, dy)));
    }
    return q;
}

static void title_layout(Title *t, const Grid *g) {
    t->font = figlet_font(FIGLET_SLANT);
    int w = t->font ? figlet_render(t->font, TITLE_TEXT, t->art, TITLE_ART_W) : -1;
    if (w < 0 || w > g->cols) {
        t->font = figlet_font(FIGLET_SMALL);
        w = t->font ? figlet_render(t->font, TITLE_TEXT, t->art, TITLE_ART_W) : -1;
    }
    if (w < 0) {
        t->font = NULL;
        return;
    }
    int h = t->font->height;
    while (h > 1) {
        int blank = 1;
        for (int x = 0; x < w; x++) blank &= t->art[(h - 1) * TITLE_ART_W + x] == ' ';
        if (!blank) break;
        h--;
    }
    t->title_w = w;
    t->title_rows = h;
    t->title_col = (g->cols - w) / 2;
    t->title_row = (int)((float)g->rows * 0.13f);
}

static void title_text(const Title *t, Grid *g, float time) {
    if (!t->font) return;
    int w = t->title_w, h = t->title_rows, col = t->title_col, row = t->title_row;
    clear_box(g, col - 1, row - 1, w + 2, h + 2);
    float sweep = mfract(time / 7.0f) * 7.0f;
    float shine_x = (sweep / 2.6f) * (float)(w + 30) - 15;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            char ch = t->art[y * TITLE_ART_W + x];
            if (ch == ' ') continue;
            Rgb color = rgb_mix(PEARL, GOLD, (float)y / (float)(h > 1 ? h - 1 : 1));
            float s = ((float)x - (float)y * 1.5f - shine_x) / 3.5f;
            float shine = sweep < 2.6f ? mexp(-s * s) : 0;
            color = rgb_mix(color, (Rgb){255, 255, 255}, shine * 0.85f);
            grid_put(g, col + x, row + y, ch, color);
        }
}

void title_draw(Title *t, double time, Screen *s) {
    float tm = (float)time;
    Grid *g = s->scene;
    Canvas *c = &t->canvas;
    prof_push("clear");
    canvas_clear(c);
    prof_pop();
    Planet p = planet_place(c->aspect);
    prof_push("planet");
    planet_body(c, &p, tm);
    prof_pop();
    prof_push("resolve");
    ascii_resolve(&t->matcher, c, g);
    prof_pop();
    Box quiet[2] = {{t->title_col, t->title_row, t->title_w, t->title_rows},
                    {g->cols / 2 - 8, g->rows / 2 + 1, 16, 7}};
    prof_push("nebula");
    nebula(g, c->aspect, tm, hush, quiet);
    prof_pop();
    prof_push("stars");
    stars(g, tm);
    prof_pop();
    prof_push("rings");
    planet_rings(&t->stroker, &p);
    comet(g, tm);
    prof_pop();
    prof_push("title");
    title_text(t, g, tm);
    prof_pop();
    fx_reset(&s->fx, SPACE);
    s->fx.glow = 0.35f;
}
