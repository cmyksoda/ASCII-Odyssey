// SPDX-License-Identifier: GPL-3.0-only
// Scenes that answer the music all the way through: every kick, snare and note shapes what they draw.
#include "mathx.h"
#include "noise.h"
#include "scene.h"

#include <string.h>

static float cell_x(const Grid *g, int col) {
    return ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect;
}

static float cell_y(const Grid *g, int row) {
    return 1 - ((float)row + 0.5f) / (float)g->rows * 2;
}

static int pitched(Role r) {
    return r == ROLE_BASS || r == ROLE_PAD || r == ROLE_CHORD || r == ROLE_ARP || r == ROLE_LEAD || r == ROLE_BELL ||
           r == ROLE_PLUCK;
}

/* ---------- Kaleidoscope ---------- */

typedef struct {
    int folds;
    float spin, zoom, salt, drift;
} Kaleidoscope;

void kaleidoscope_setup(void *state, Rng *r, const Frame *f) {
    Kaleidoscope *k = state;
    static const int folds[4] = {5, 6, 8, 12};
    k->folds = folds[rng_range(r, 0, 4)];
    k->spin = rng_between(r, 0.06f, 0.16f) * (rng_chance(r, 50) ? 1 : -1);
    k->zoom = rng_between(r, 1.5f, 2.3f);
    k->salt = rng_between(r, 0, 60);
    k->drift = rng_between(r, 0.1f, 0.2f);
    (void)f;
}

// A mandala: slow-moving shapes caught in one mirrored wedge and repeated round the center. Each kick punches the
// zoom and sends a bright ring outward, each snare turns the figure a step, and chord changes shift its colors.
void kaleidoscope_draw(void *state, const Frame *f) {
    Kaleidoscope *k = state;
    Grid *g = f->grid;
    static const char ramp[] = " .:-+*#";
    float kick = frame_pulse(f, ROLE_KICK, 0.22f, 1.0f);
    float turn = f->local * k->spin + frame_surge(f, ROLE_SNARE, 0.25f, 0.4f);
    float zoom = k->zoom * (1 - 0.15f * kick), t = f->local * k->drift + frame_travel(f, 0, 0.08f);
    const ChordMark *chord = score_chord(f->score, f->t);
    float shift = chord ? (float)chord->chord.root_pc / 12.0f : 0, wedge = TAU_F / (float)k->folds;
    const ScoreEvent *kicks[6];
    int n_kicks = recent_notes(f->score, ROLE_KICK, f->t, 1.2f, kicks, 6);
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), radius = mhypot(x, y);
            float frame = 1 - smoothstep(0.82f, 1.0f, radius);
            if (frame <= 0) continue;
            float a = matan2(y, x) + turn;
            a -= mfloor(a / wedge) * wedge;
            if (a > wedge * 0.5f) a = wedge - a;
            float u = radius * mcos(a) * zoom, v = radius * msin(a) * zoom;
            // A petal in every wedge and rings drifting outward, so the figure keeps a flower's bones when the noise
            // it's built from runs thin.
            float petal = (0.5f + 0.5f * mcos(a / wedge * TAU_F)) * (0.5f + 0.5f * mcos(radius * 7 * zoom - t * 6));
            float shape = smoothstep(0.42f, 0.72f, noise_fbm(u + k->salt + t, v - t * 0.6f) * 0.7f + petal * 0.4f + 0.02f);
            // Rings racing outward from the center, one for each recent kick.
            float rings = 0;
            for (int i = 0; i < n_kicks; i++) {
                float d = radius - (f->t - kicks[i]->time) * 1.1f;
                rings = mmax(rings, mexp(-d * d * 300) * kicks[i]->velocity * frame_bright(f));
            }
            float level = saturate((shape * 0.9f + rings) * frame);
            int i = (int)(level * 6.99f);
            if (i <= 0) continue;
            float hue = mfract(radius * 0.6f + shift);
            Rgbf color = rgbf_mix(palette_ramp(f->palette, 0.3f + 0.7f * level), palette_accent(f->palette, (int)(hue * 3)), 0.45f * (1 - level * 0.5f));
            if (rings > 0.3f) color = rgbf_mix(color, palette_ramp(f->palette, 1.0f), rings * 0.8f);
            *grid_at(g, col, row) = (Cell){(uint8_t)ramp[i], rgb_from_f(color), 0};
        }
    }
    f->fx->glow = 0.55f + 0.35f * kick;
}

/* ---------- Spectrum ---------- */

typedef struct {
    int width, mirror;
    float decay;
} Spectrum;

void spectrum_setup(void *state, Rng *r, const Frame *f) {
    Spectrum *s = state;
    s->width = rng_chance(r, 50) ? 3 : 2;
    s->mirror = rng_chance(r, 70);
    s->decay = rng_between(r, 0.35f, 0.6f);
    (void)f;
}

// Every note the music plays, as an equalizer: pitch picks the bar, loudness its height, and drums light the ends.
void spectrum_draw(void *state, const Frame *f) {
    Spectrum *s = state;
    Grid *g = f->grid;
    int bars = g->cols / s->width;
    if (bars > 64) bars = 64;
    float level[64] = {0}, peak[64] = {0};
    for (int role = ROLE_KICK; role < ROLE_COUNT; role++) {
        const ScoreEvent *notes[48];
        int n = recent_notes(f->score, (Role)role, f->t, 1.5f, notes, 48);
        for (int i = 0; i < n; i++) {
            float age = f->t - notes[i]->time, v = notes[i]->velocity;
            // Calmer pictures let the bars fall slowly instead of jumping.
            float tau = (role == ROLE_PAD || role == ROLE_CHORD ? s->decay * 3 : s->decay) * (f->calm ? 2.5f : 1.0f);
            float now = v * mexp(-age / tau), held = v - age * 0.9f;
            // A note lights its own bar and, more faintly, its overtones, the way a real analyzer would show it.
            static const int overtones[4] = {0, 12, 19, 24};
            static const float weights[4] = {1, 0.55f, 0.35f, 0.25f};
            for (int o = 0; o < (pitched((Role)role) ? 4 : 1); o++) {
                int at;
                if (pitched((Role)role))
                    at = (int)((float)(notes[i]->note + overtones[o] - 13) / 96.0f * (float)bars);
                else
                    at = role == ROLE_KICK ? 1 : role == ROLE_SNARE || role == ROLE_PERC ? bars / 3 : bars - 3;
                for (int d = -2; d <= 2; d++) {
                    int b = at + d;
                    if (b < 0 || b >= bars) continue;
                    float w = weights[o] * (d == 0 ? 1.0f : d == 1 || d == -1 ? 0.6f : 0.3f);
                    level[b] = mmax(level[b], now * w);
                    peak[b] = mmax(peak[b], held * w);
                }
            }
        }
    }
    int base = s->mirror ? g->rows * 2 / 3 : g->rows - 2, height = base - 1;
    // A floor that rolls across the bars like a slow swell, so the analyzer breathes between notes and in silence.
    for (int b = 0; b < bars; b++) {
        float x = (float)b / (float)bars;
        float swell = 0.5f + 0.25f * msin(f->t * 1.1f + x * 9.0f) + 0.25f * msin(f->t * 0.7f - x * 5.0f + 1.3f);
        level[b] = mmax(level[b], (0.06f + 0.12f * f->energy) * swell + 0.03f * msin(f->t * 6.1f + (float)b * 1.7f));
    }
    for (int b = 0; b < bars; b++) {
        int top = (int)(saturate(level[b]) * (float)height), cap = (int)(saturate(peak[b]) * (float)height);
        for (int k = 0; k < s->width - 1; k++) {
            int col = b * s->width + k + (g->cols - bars * s->width) / 2;
            for (int h = 0; h < top; h++) {
                float v = (float)h / (float)height;
                Rgbf color = rgbf_mix(palette_ramp(f->palette, 0.45f + 0.55f * v), palette_accent(f->palette, b * 3 / bars), v * v * 0.6f);
                put_cell(g, col, base - 1 - h, h % 2 ? '=' : '#', color);
                if (s->mirror && h < g->rows - base) put_cell(g, col, base + h, h % 2 ? '.' : ':', frame_dim(f, color, 0.35f * (1 - (float)h / (float)(g->rows - base))));
            }
            if (cap > top) put_cell(g, col, base - 1 - cap, '_', palette_accent(f->palette, 1));
        }
    }
    for (int col = 0; col < g->cols; col++) put_cell_under(g, col, base, '-', palette_ramp(f->palette, 0.25f));
    f->fx->glow = 0.45f;
}

/* ---------- Ripples ---------- */

typedef struct {
    float speed, cx, cy;
    uint32_t salt;
} Ripples;

void ripples_setup(void *state, Rng *r, const Frame *f) {
    Ripples *p = state;
    p->speed = rng_between(r, 0.7f, 1.2f);
    p->cx = rng_between(r, -0.3f, 0.3f) * f->grid->aspect;
    p->cy = rng_between(r, -0.2f, 0.2f);
    p->salt = rng_u32(r);
}

static void ring(const Frame *f, float x, float y, float radius, Rgbf color) {
    StrokePoint pts[64];
    for (int i = 0; i < 64; i++) {
        float a = (float)i / 64.0f * TAU_F;
        pts[i] = (StrokePoint){x + mcos(a) * radius, y + msin(a) * radius, color};
    }
    stroke_draw(f->stroker, pts, 64, 1);
}

// Rings spread from every hit: kicks from the middle, snares and claps from wherever they land, hats as small drops.
void ripples_draw(void *state, const Frame *f) {
    Ripples *p = state;
    Grid *g = f->grid;
    Canvas *c = f->canvas;
    canvas_clear(c);
    float breathe = 0.03f * msin(f->t * 0.8f);
    bokeh(c, p->cx, p->cy, 0.25f + breathe + 0.1f * frame_pulse(f, ROLE_KICK, 0.2f, 1.0f),
          0.35f + breathe + 0.4f * frame_pulse(f, ROLE_KICK, 0.2f, 1.0f), palette_accent(f->palette, 0));
    frame_resolve(f);
    dust_stars(f, p->salt, 0.0f, 0.5f);
    static const Role roles[4] = {ROLE_KICK, ROLE_SNARE, ROLE_PERC, ROLE_HAT};
    for (int k = 0; k < 4; k++) {
        const ScoreEvent *notes[16];
        int n = recent_notes(f->score, roles[k], f->t, roles[k] == ROLE_HAT ? 0.6f : 2.2f, notes, 16);
        for (int i = 0; i < n; i++) {
            float age = f->t - notes[i]->time;
            uint32_t id = (uint32_t)(notes[i]->time * 977.0f) ^ p->salt;
            float x = roles[k] == ROLE_KICK ? p->cx : (hash01(id) * 1.6f - 0.8f) * g->aspect;
            float y = roles[k] == ROLE_KICK ? p->cy : hash01(id + 1) * 1.4f - 0.7f;
            float radius = age * p->speed * (roles[k] == ROLE_HAT ? 0.4f : 0.8f) + 0.03f;
            float fade = mexp(-age / (roles[k] == ROLE_HAT ? 0.25f : 0.8f)) * notes[i]->velocity * frame_bright(f);
            Rgbf color = frame_dim(f, roles[k] == ROLE_KICK ? palette_ramp(f->palette, 0.95f) : palette_accent(f->palette, k), 0.3f + fade);
            if (fade > 0.05f) ring(f, x, y, radius, color);
            if (fade > 0.25f && roles[k] != ROLE_HAT) ring(f, x, y, radius * 0.6f, frame_dim(f, color, 0.5f));
        }
    }
    // Melody notes fall like drops on the water, across the width by pitch.
    static const Role sung[3] = {ROLE_LEAD, ROLE_BELL, ROLE_PLUCK};
    for (int k = 0; k < 3; k++) {
        const ScoreEvent *notes[12];
        int n = recent_notes(f->score, sung[k], f->t, 1.8f, notes, 12);
        for (int i = 0; i < n; i++) {
            float age = f->t - notes[i]->time, fade = mexp(-age / 0.7f) * notes[i]->velocity * frame_bright(f);
            if (fade < 0.05f) continue;
            uint32_t id = (uint32_t)(notes[i]->time * 613.0f) ^ p->salt;
            float x = ((float)(notes[i]->note % 36) / 36.0f * 1.6f - 0.8f) * g->aspect, y = hash01(id) * 1.3f - 0.65f;
            ring(f, x, y, age * p->speed * 0.5f + 0.02f, frame_dim(f, palette_accent(f->palette, k + 1), 0.3f + fade));
        }
    }
    // And a slow rain of quiet drops that never stops, music or not.
    for (int j = 0; j < 4; j++) {
        float slot = mfloor(f->t / 0.7f) - (float)j, age = f->t - slot * 0.7f;
        uint32_t id = (uint32_t)slot * 2654435761u ^ p->salt;
        float x = (hash01(id) * 1.8f - 0.9f) * g->aspect, y = hash01(id + 1) * 1.6f - 0.8f;
        float fade = mexp(-age / 1.0f) * 0.35f;
        if (fade > 0.04f) ring(f, x, y, age * p->speed * 0.35f + 0.02f, frame_dim(f, palette_ramp(f->palette, 0.6f), fade + 0.15f));
    }
    f->fx->glow = 0.55f;
}

/* ---------- Fireworks ---------- */

typedef struct {
    float rise, spread, gravity;
    uint32_t salt;
    int hills;
} Fireworks;

void fireworks_setup(void *state, Rng *r, const Frame *f) {
    Fireworks *w = state;
    w->rise = rng_between(r, 0.55f, 0.8f);
    w->spread = rng_between(r, 0.45f, 0.65f);
    w->gravity = rng_between(r, 0.16f, 0.26f);
    w->salt = rng_u32(r);
    w->hills = rng_chance(r, 60);
    (void)f;
}

// One shell: sparks fly out, slow, fall and fade, each trailing a short tail of embers.
static void burst(const Frame *f, const Fireworks *w, float bx, float by, float age, uint32_t id, float flare, float size) {
    Grid *g = f->grid;
    Rgbf color = palette_accent(f->palette, (int)(id % 3)), second = palette_accent(f->palette, (int)(id % 3) + 1);
    Rgbf white = palette_ramp(f->palette, 1.0f);
    int sparks = (int)((float)(36 + (int)(id % 20)) * size);
    float life = mexp(-age / 1.1f);
    for (int s = 0; s < sparks; s++) {
        float a = (float)s / (float)sparks * TAU_F + hash01(id + (uint32_t)s) * 0.2f;
        float speed = w->spread * size * (0.75f + 0.25f * hash01(id * 7u + (uint32_t)s));
        // Calmer pictures skip the white burst: sparks start in their own color and burn lower.
        Rgbf c = rgbf_mix(white, s % 2 ? color : second, f->calm ? 1.0f : saturate(age * 2.5f));
        for (int k = 0; k < 4; k++) {
            float at = mmax(0, age - (float)k * 0.07f), reach = speed * (1 - mexp(-at * 2.4f));
            float x = bx + mcos(a) * reach * g->aspect * 0.75f, y = by + msin(a) * reach - w->gravity * at * at;
            float bright = (k ? 0.55f / (float)(k + 1) : 1.0f) * (0.2f + life) * (1 + flare) * (0.4f + 0.6f * frame_bright(f));
            int glyph = k ? (k == 1 ? '+' : '.') : age < 0.3f ? '*' : life > 0.45f ? '*' : '+';
            if (bright > 0.06f) (k ? put_cell_under : put_cell)(g, cell_col(g, x), cell_row(g, y), glyph, frame_dim(f, c, bright));
        }
    }
    // The flash at the heart of the burst.
    if (age < 0.18f && !f->calm) put_cell(g, cell_col(g, bx), cell_row(g, by), '#', frame_dim(f, white, 1.2f));
}

static void rocket(const Frame *f, float x, float y) {
    Grid *g = f->grid;
    put_cell(g, cell_col(g, x), cell_row(g, y), '|', palette_ramp(f->palette, 1.0f));
    for (int t = 1; t < 4; t++)
        put_cell_under(g, cell_col(g, x), cell_row(g, y) + t, t == 1 ? ':' : '.', frame_dim(f, palette_accent(f->palette, 1), 0.8f / (float)t));
}

// Rockets climb so that they burst exactly on the snares and claps, the score telling them when; every kick makes the
// sparks flare. Through bars with no drums to cue them, a smaller shell bursts on the downbeat.
void fireworks_draw(void *state, const Frame *f) {
    Fireworks *w = state;
    Grid *g = f->grid;
    dust_stars(f, w->salt, 0.0f, 0.45f);
    float flare = frame_pulse(f, ROLE_KICK, 0.15f, 0.6f);
    static const Role cues[5] = {ROLE_SNARE, ROLE_PERC, ROLE_CRASH, ROLE_BELL, ROLE_LEAD};
    for (int k = 0; k < 5; k++) {
        const ScoreEvent *notes[24];
        int n = recent_notes(f->score, cues[k], f->t + w->rise, 2.8f + w->rise, notes, 24);
        for (int i = 0; i < n; i++) {
            uint32_t id = (uint32_t)(notes[i]->time * 613.0f) ^ w->salt;
            // Only some melody notes launch a rocket, or a busy tune would fill the sky.
            if (k >= 3 && hash01(id + 9) > 0.3f) continue;
            float bx = (hash01(id) * 1.4f - 0.7f) * g->aspect, by = 0.1f + hash01(id + 3) * 0.6f;
            float age = f->t - notes[i]->time;
            if (age < 0) rocket(f, bx, -1 + (by + 1) * ease_out(1 + age / w->rise));
            else if (age < 2.8f) burst(f, w, bx, by, age, id, flare, 1.0f);
        }
    }
    float bar = f->score->beat * (float)f->score->info->beats_per_bar;
    for (int b = (int)mfloor((f->t + w->rise) / bar); b >= 0 && (float)b * bar > f->t - 2.8f; b--) {
        float when = (float)b * bar, age = f->t - when;
        if (score_count(f->score, ROLE_SNARE, when - bar, when + bar) + score_count(f->score, ROLE_PERC, when - bar, when + bar) +
                score_count(f->score, ROLE_CRASH, when - bar, when + bar) > 0)
            continue;
        uint32_t id = ((uint32_t)b * 2654435761u) ^ w->salt ^ 0xb0b0u;
        float bx = (hash01(id) * 1.4f - 0.7f) * g->aspect, by = 0.2f + hash01(id + 3) * 0.45f;
        if (age < 0) rocket(f, bx, -1 + (by + 1) * ease_out(1 + age / w->rise));
        else burst(f, w, bx, by, age, id, flare, 0.65f);
    }
    if (w->hills) {
        StrokePoint pts[48];
        for (int i = 0; i < 48; i++) {
            float x = -g->aspect + (float)i / 47.0f * 2 * g->aspect;
            pts[i] = (StrokePoint){x, -0.82f + 0.1f * noise_fbm(x * 0.8f + (float)(w->salt % 97), 2), palette_ramp(f->palette, 0.35f)};
        }
        stroke_draw(f->stroker, pts, 48, 0);
    }
    f->fx->glow = 0.7f + flare * 0.4f;
}

/* ---------- Lasers ---------- */

typedef struct {
    int emitters, from_top;
    float sweep, rate, phase[6];
} Lasers;

void lasers_setup(void *state, Rng *r, const Frame *f) {
    Lasers *l = state;
    l->emitters = rng_range(r, 3, 7);
    l->from_top = rng_chance(r, 30);
    l->sweep = rng_between(r, 0.4f, 0.9f);
    l->rate = rng_between(r, 0.3f, 0.7f);
    for (int i = 0; i < 6; i++) l->phase[i] = rng_float(r) * TAU_F;
    (void)f;
}

// Beams fan out from a row of emitters: they sweep on their own, jump on every kick, and fling wide on the snare.
void lasers_draw(void *state, const Frame *f) {
    Lasers *l = state;
    Grid *g = f->grid;
    float kick = frame_pulse(f, ROLE_KICK, 0.18f, 1.0f), snare = frame_pulse(f, ROLE_SNARE, 0.3f, 1.0f);
    float jump = frame_surge(f, ROLE_KICK, 0.2f, 0.6f), spread = 1 + 0.6f * snare;
    int bar = (int)(f->t / (float)f->score->info->bar_seconds);
    float ey = l->from_top ? 1.05f : -1.05f;
    for (int i = 0; i < l->emitters; i++) {
        float ex = ((float)i + 0.5f) / (float)l->emitters * 2 * g->aspect * 0.8f - g->aspect * 0.8f;
        for (int b = 0; b < 3; b++) {
            float a = (float)(b - 1) * 0.35f * spread + l->sweep * msin(f->local * l->rate + l->phase[i] + jump) + (l->from_top ? PI_F : 0);
            float dx = msin(a), dy = mcos(a);
            Rgbf color = frame_dim(f, palette_accent(f->palette, i + bar), 0.55f + 0.6f * kick);
            StrokePoint beam[2] = {{ex, ey, color}, {ex + dx * 3, ey + dy * 3, frame_dim(f, color, 0.4f)}};
            stroke_draw(f->stroker, beam, 2, 0);
        }
        put_cell(g, cell_col(g, ex), cell_row(g, ey * 0.97f), 'O', frame_dim(f, palette_ramp(f->palette, 1.0f), 0.6f + 0.6f * kick));
    }
    // Haze the beams light up: a drift of faint specks that brightens with the kick.
    for (int s = 0; s < g->cols * g->rows / 18; s++) {
        uint32_t id = (uint32_t)s * 2246822519u;
        int col = (int)(hash01(id) * (float)g->cols), row = (int)(mfract(hash01(id + 1) + f->t * 0.02f) * (float)g->rows);
        put_cell_under(g, col, row, '.', frame_dim(f, palette_ramp(f->palette, 0.5f), 0.25f + 0.4f * kick));
    }
    f->fx->glow = 0.7f;
}

/* ---------- Lissajous ---------- */

typedef struct {
    float drift, size;
    int trails;
} Lissajous;

void lissajous_setup(void *state, Rng *r, const Frame *f) {
    Lissajous *s = state;
    s->drift = rng_between(r, 0.2f, 0.5f);
    s->size = rng_between(r, 0.6f, 0.82f);
    s->trails = rng_range(r, 3, 6);
    (void)f;
}

typedef struct {
    float a, b, delta, sx, sy;
    Rgbf color;
} Figure;

static StrokePoint figure_at(float t, const void *context) {
    const Figure *fg = context;
    float s = t * TAU_F;
    return (StrokePoint){fg->sx * msin(fg->a * s + fg->delta), fg->sy * msin(fg->b * s), fg->color};
}

// An oscilloscope tracing the harmony: its loops follow the ratio of the chord's tones, bass notes turn its phase,
// and the kick swells it.
void lissajous_draw(void *state, const Frame *f) {
    Lissajous *s = state;
    Grid *g = f->grid;
    // The graticule, faint, like the screen of a real scope.
    for (int row = 2; row < g->rows; row += 5)
        for (int col = 3; col < g->cols; col += 8) put_cell(g, col, row, '+', palette_ramp(f->palette, 0.2f));
    const ChordMark *chord = score_chord(f->score, f->t);
    static const float ratios[4][2] = {{2, 3}, {4, 5}, {5, 6}, {3, 4}};
    int kind = chord ? (chord_minor(&chord->chord) ? 2 : chord->chord.root_pc % 2 ? 3 : 1) : 0;
    float kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    float delta = f->local * s->drift + frame_surge(f, ROLE_BASS, 0.3f, 0.8f);
    for (int k = s->trails; k >= 0; k--) {
        Figure fg = {ratios[kind][0], ratios[kind][1], delta - (float)k * 0.06f, s->size * g->aspect * (1 + 0.12f * kick),
                     s->size * (1 + 0.12f * kick),
                     frame_dim(f, k ? palette_ramp(f->palette, 0.5f) : palette_ramp(f->palette, 1.0f), k ? 0.7f / (float)(k + 1) : 1.0f)};
        stroke_curve(f->stroker, figure_at, &fg, 220, 1);
    }
    f->fx->glow = 0.75f;
}

/* ---------- Skyline ---------- */

typedef struct {
    int count, x[24], w[24], h[24];
    uint32_t salt;
} Skyline;

void skyline_setup(void *state, Rng *r, const Frame *f) {
    Skyline *s = state;
    Grid *g = f->grid;
    s->count = 0;
    for (int x = rng_range(r, -2, 2); x < g->cols && s->count < 24; x += s->w[s->count - 1] + rng_range(r, 0, 3)) {
        s->x[s->count] = x;
        s->w[s->count] = rng_range(r, 5, 11);
        s->h[s->count] = rng_range(r, g->rows / 5, g->rows * 3 / 5);
        s->count++;
    }
    s->salt = rng_u32(r);
}

// A city at night whose windows light up note by note and its rooftop beacons blink on the kick. Between notes the
// city keeps its own life: lights go on and off in their own time, traffic streams along the street, and now and
// then a plane blinks across the sky.
void skyline_draw(void *state, const Frame *f) {
    Skyline *s = state;
    Grid *g = f->grid;
    // The street sits a few rows up, clear of the edge a television's overscan can crop.
    int street = g->rows - 5;
    dust_stars(f, s->salt, 0.003f, 0.6f);
    moon_disc(f, g->aspect * 0.55f, 0.62f, 0.12f, palette_ramp(f->palette, 0.9f), s->salt, 0);
    float flight = mfract(f->t / 26 + hash01(s->salt + 5));
    if (flight < 0.5f) {
        float x = -g->aspect + flight * 2 * 2.2f * g->aspect - 0.1f * g->aspect, y = 0.78f + 0.08f * hash01(s->salt + 6);
        int blink = mfract(f->t * 1.1f) < 0.15f;
        put_cell(g, cell_col(g, x), cell_row(g, y), blink ? '*' : '.', blink ? palette_accent(f->palette, 0) : palette_ramp(f->palette, 0.6f));
    }
    float lit[24] = {0};
    for (int role = ROLE_BASS; role < ROLE_FX; role++) {
        const ScoreEvent *notes[24];
        int n = recent_notes(f->score, (Role)role, f->t, 2.0f, notes, 24);
        for (int i = 0; i < n; i++) {
            int b = (int)(hash_u32((uint32_t)notes[i]->note * 31u + (uint32_t)role) % (uint32_t)s->count);
            lit[b] = mmax(lit[b], notes[i]->velocity * mexp(-(f->t - notes[i]->time) / 0.7f) * frame_bright(f));
        }
    }
    float kick = frame_pulse(f, ROLE_KICK, 0.15f, 1.0f);
    Rgbf wall = palette_ramp(f->palette, 0.28f), warm = palette_accent(f->palette, 1);
    for (int b = 0; b < s->count; b++) {
        int top = street - s->h[b];
        for (int y = top; y < street; y++)
            for (int x = s->x[b]; x < s->x[b] + s->w[b]; x++) {
                int edge = x == s->x[b] || x == s->x[b] + s->w[b] - 1;
                if (y == top) {
                    put_cell(g, x, y, edge ? '.' : '_', wall);
                } else if (edge) {
                    put_cell(g, x, y, '|', wall);
                } else if ((x - s->x[b]) % 2 == 1 && (y - top) % 2 == 0) {
                    // Each window rethinks whether it's lit about every forty seconds, all at their own moments.
                    uint32_t id = (uint32_t)(x * 131 + y * 977) ^ s->salt;
                    float h = hash01(id), mood = hash01(id ^ (uint32_t)mfloor(f->t / 40 + h * 40) * 2654435761u);
                    float on = mood < 0.18f ? 0.6f : h < 0.18f + lit[b] * 0.7f ? lit[b] : 0;
                    put_cell(g, x, y, on > 0.3f ? '#' : '.', on > 0 ? rgbf_mix(wall, warm, saturate(on * 1.4f)) : frame_dim(f, wall, 0.7f));
                } else {
                    put_cell(g, x, y, ' ', wall);
                }
            }
        if (s->h[b] > g->rows * 2 / 5) {
            int blink = kick > 0.2f || mfract(f->t * 0.5f + (float)b * 0.37f) < 0.12f;
            put_cell(g, s->x[b] + s->w[b] / 2, top - 1, blink ? '*' : '.', palette_accent(f->palette, 0));
        }
    }
    // The street: headlights leading and taillights trailing, one lane each way, then the far curb.
    for (int col = 0; col < g->cols; col++) {
        put_cell(g, col, street, '_', wall);
        put_cell(g, col, street + 3, '-', frame_dim(f, wall, 0.6f));
    }
    Rgbf head = palette_ramp(f->palette, 1.0f), tail = palette_accent(f->palette, 0);
    for (int lane = 0; lane < 2; lane++) {
        int dir = lane ? -1 : 1;
        float span = (float)g->cols + 24, speed = lane ? 13.0f : 9.0f;
        for (int car = 0; car < 5; car++) {
            uint32_t id = s->salt + (uint32_t)(lane * 16 + car + 1) * 2654435761u;
            float along = mfract(hash01(id) + f->t * speed / span) * span - 12;
            int front = (int)(dir > 0 ? along : (float)g->cols - along);
            put_cell(g, front, street + 1 + lane, 'o', head);
            for (int k = 1; k <= 3; k++)
                put_cell(g, front - dir * k, street + 1 + lane, k == 3 ? '.' : '=', frame_dim(f, k == 3 ? tail : wall, k == 3 ? 1.0f : 1.6f));
        }
    }
    f->fx->glow = 0.5f;
}

/* ---------- Digital rain ---------- */

typedef struct {
    float speed, density;
    uint32_t salt;
} DigitalRain;

void digital_rain_setup(void *state, Rng *r, const Frame *f) {
    DigitalRain *d = state;
    d->speed = rng_between(r, 14, 24);
    d->density = rng_between(r, 0.35f, 0.6f);
    d->salt = rng_u32(r);
    (void)f;
}

static const char GLYPHS[] = "0123456789ABCDEF<>=+*:;|/\\[]{}#$%&?!";

static void rain_column(const Frame *f, int col, float head, int length, float t, uint32_t id, Rgbf lead, Rgbf body) {
    Grid *g = f->grid;
    int top = (int)head;
    for (int k = 0; k < length; k++) {
        int row = top - k;
        if (row < 0 || row >= g->rows) continue;
        uint32_t roll = hash_u32(id + (uint32_t)row * 97u + (uint32_t)(t * (k ? 6.0f : 14.0f)));
        float fade = 1 - (float)k / (float)length;
        put_cell(g, col, row, GLYPHS[roll % (sizeof GLYPHS - 1)], k ? frame_dim(f, body, 0.15f + 0.85f * fade * fade) : lead);
    }
}

// Columns of falling symbols, as in a terminal dreaming: notes drop bright new streams at their pitch, and the kick
// flashes every leading glyph.
void digital_rain_draw(void *state, const Frame *f) {
    DigitalRain *d = state;
    Grid *g = f->grid;
    float kick = frame_pulse(f, ROLE_KICK, 0.15f, 1.0f), speed = d->speed * (0.6f + 0.6f * f->energy);
    Rgbf body = palette_ramp(f->palette, 0.75f), lead = rgbf_mix(palette_ramp(f->palette, 1.0f), palette_accent(f->palette, 1), kick);
    for (int col = 0; col < g->cols; col++) {
        uint32_t id = d->salt + (uint32_t)col * 2654435761u;
        if (hash01(id) > d->density) continue;
        float span = (float)g->rows * 2.2f, offset = hash01(id + 1) * span, rate = speed * (0.6f + 0.8f * hash01(id + 2));
        float head = mfract((offset + f->local * rate) / span) * span;
        rain_column(f, col, head, 6 + (int)(hash01(id + 3) * 14), f->t, id, lead, body);
    }
    for (int role = ROLE_BASS; role < ROLE_FX; role++) {
        const ScoreEvent *notes[16];
        int n = recent_notes(f->score, (Role)role, f->t, 1.5f, notes, 16);
        for (int i = 0; i < n; i++) {
            int col = (int)((float)(notes[i]->note % 48) / 48.0f * (float)g->cols + hash01((uint32_t)i) * 3);
            float head = (f->t - notes[i]->time) * speed * 1.4f;
            rain_column(f, col, head, 10, f->t, (uint32_t)notes[i]->note * 7919u, frame_dim(f, palette_accent(f->palette, role), 1.2f),
                        palette_accent(f->palette, role));
        }
    }
    f->fx->glow = 0.55f + 0.3f * kick;
}

/* ---------- Checker floor ---------- */

typedef struct {
    float speed, sway, horizon, scale;
    int ceiling;
    uint32_t salt;
} Checker;

void checker_setup(void *state, Rng *r, const Frame *f) {
    Checker *c = state;
    c->speed = rng_between(r, 1.5f, 2.6f);
    c->sway = rng_between(r, 0.15f, 0.4f);
    c->horizon = rng_between(r, 0.0f, 0.2f);
    c->scale = rng_between(r, 2.5f, 3.5f);
    c->ceiling = rng_chance(r, 40);
    c->salt = rng_u32(r);
    (void)f;
}

// An endless checkerboard rushing toward the viewer under a glowing horizon: it lunges forward on every kick, and the
// chords swap its colors.
void checker_draw(void *state, const Frame *f) {
    Checker *c = state;
    Grid *g = f->grid;
    if (!c->ceiling) dust_stars(f, c->salt, 0.004f, 0.7f);
    float travel = frame_travel(f, c->speed * 0.4f, c->speed) + frame_surge(f, ROLE_KICK, 0.2f, 0.9f);
    float side = c->sway * msin(f->local * 0.35f), kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    const ChordMark *chord = score_chord(f->score, f->t);
    int swap = chord ? chord->chord.root_pc % 3 : 0;
    Rgbf tile = palette_accent(f->palette, swap), glow = palette_ramp(f->palette, 0.9f);
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row) - c->horizon;
        if (y > 0 && !c->ceiling) continue;
        float h = mabs(y);
        if (h < 0.025f) continue;
        float depth = 0.5f / h, fog = saturate(1.6f / depth);
        for (int col = 0; col < g->cols; col++) {
            float x = (cell_x(g, col) + side) * depth * c->scale, z = (depth + travel) * c->scale;
            int parity = (mfloor_i(x) + mfloor_i(z)) & 1;
            if (!parity) continue;
            // Tile edges draw as lines near the viewer and fade to dots in the distance.
            float fx = mfract(x), fz = mfract(z);
            int edge = fx < 0.12f || fx > 0.88f || fz < 0.12f || fz > 0.88f;
            int glyph = fog > 0.5f ? (edge ? '+' : '#') : fog > 0.22f ? (edge ? '.' : '=') : '.';
            Rgbf color = rgbf_mix(frame_dim(f, tile, fog * (0.75f + 0.45f * kick)), glow, (1 - fog) * 0.35f);
            *grid_at(g, col, row) = (Cell){(uint8_t)glyph, rgb_from_f(color), 0};
        }
    }
    // The horizon line, bright and pulsing.
    int hz = cell_row(g, c->horizon);
    for (int col = 0; col < g->cols; col++) put_cell(g, col, hz, '=', frame_dim(f, glow, 0.7f + 0.4f * kick));
    f->fx->glow = 0.55f;
}
