// SPDX-License-Identifier: GPL-3.0-or-later
// Scenes that joined after 1.0: beads on threads, a crowd at a show, a sea chart of the song, and the game of life.
#include "mathx.h"
#include "scene.h"

#include <stdio.h>
#include <string.h>

static void text_at(Grid *g, int col, int row, const char *s, Rgbf color) {
    for (int k = 0; s[k]; k++) put_cell(g, col + k, row, s[k], color);
}

/* ---------- Beads ---------- */

#define THREADS 7

typedef struct {
    // The thread each part of the band is strung on, or -1 when it has none.
    int count, thread[THREADS];
    float sag[THREADS], tilt[THREADS];
    uint32_t salt;
} Beads;

// The part each role plays in, from the top thread down, and the bead its notes become, two cells wide. Hats and
// effects would crowd a thread, so they have no bead.
static const struct {
    int part;
    const char *bead;
} BEADS[ROLE_COUNT] = {[ROLE_LEAD] = {0, "()"},  [ROLE_BELL] = {1, "<>"}, [ROLE_PLUCK] = {2, "oo"},
                       [ROLE_ARP] = {3, "**"},   [ROLE_CHORD] = {4, "{}"}, [ROLE_PAD] = {4, "{}"},
                       [ROLE_BASS] = {5, "[]"},  [ROLE_KICK] = {6, "@@"}, [ROLE_SNARE] = {6, "##"},
                       [ROLE_PERC] = {6, "::"}};

void beads_setup(void *state, Rng *r, const Frame *f) {
    Beads *b = state;
    int notes[THREADS] = {0}, parts = 0, most = f->grid->rows / 5;
    for (int role = 0; role < ROLE_COUNT; role++)
        if (BEADS[role].bead) notes[BEADS[role].part] += f->score->role_count[role];
    for (int p = 0; p < THREADS; p++) parts += notes[p] > 0;
    // When the screen is too short for every part, the ones that play the least go unstrung.
    for (; parts > most; parts--) {
        int least = -1;
        for (int p = 0; p < THREADS; p++)
            if (notes[p] > 0 && (least < 0 || notes[p] < notes[least])) least = p;
        notes[least] = 0;
    }
    b->count = 0;
    for (int p = 0; p < THREADS; p++) {
        b->thread[p] = notes[p] > 0 ? b->count++ : -1;
        b->sag[p] = rng_between(r, 0.1f, 0.2f);
        b->tilt[p] = rng_between(r, -0.04f, 0.04f);
    }
    b->salt = rng_u32(r);
}

// The first of the score's events later than t.
static int events_after(const Score *s, float t) {
    int lo = 0, hi = s->count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (s->events[mid].time <= t)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static int bead_thread(const Beads *b, const ScoreEvent *e) {
    return BEADS[e->role].bead ? b->thread[BEADS[e->role].part] : -1;
}

// When each thread was last plucked by a bead landing on it, and how hard.
typedef struct {
    float at[THREADS], strength[THREADS];
} Plucks;

// How high thread k hangs at u, from 0 at its left pin to 1 at its right: sagging between the pins, and still
// ringing from its last pluck.
static float thread_y(const Beads *b, const Frame *f, const Plucks *plucks, int k, float u) {
    float y = 0.86f - 1.72f * ((float)k + 0.5f) / (float)b->count + b->tilt[k] * (1 - 2 * u) -
              b->sag[k] * 4 * u * (1 - u);
    if (plucks->strength[k] <= 0) return y;
    float age = f->t - plucks->at[k], one_row = 2.0f / (float)f->grid->rows;
    float swing = plucks->strength[k] * mexp(-age / 0.35f) * one_row * frame_pace(f) * f->react;
    return y + swing * (msin(PI_F * u) * mcos(age * 31.0f) + 0.4f * msin(TAU_F * u) * mcos(age * 62.0f));
}

// Every part of the band is a thread, and every note a bead sliding along it from the right. A bead reaches the
// strand already strung on the left exactly as its note sounds, pushes the others along, and plucks the thread.
void beads_draw(void *state, const Frame *f) {
    Beads *b = state;
    Grid *g = f->grid;
    const Score *s = f->score;
    dust_stars(f, b->salt, 0.002f, 0.3f);
    float row_time = (float)s->info->bar_seconds / (float)s->info->rows_per_bar, speed = 2.0f / row_time;
    float reach = g->aspect + 0.05f;
    int clasp = g->cols / 3 & ~1;

    Plucks plucks = {{0}, {0}};
    for (int i = events_after(s, f->t) - 1, found = 0; i >= 0 && found < b->count; i--) {
        const ScoreEvent *e = &s->events[i];
        int k = bead_thread(b, e);
        if (e->time < f->t - 2.0f) break;
        if (k < 0 || plucks.strength[k] > 0) continue;
        plucks.at[k] = e->time, plucks.strength[k] = e->velocity, found++;
    }

    Rgbf string = frame_dim(f, palette_ramp(f->palette, 0.4f), 0.8f);
    for (int k = 0; k < b->count; k++) {
        StrokePoint pts[32];
        for (int i = 0; i < 32; i++) {
            float u = (float)i / 31;
            pts[i] = (StrokePoint){(u * 2 - 1) * reach, thread_y(b, f, &plucks, k, u), string};
        }
        stroke_draw(f->stroker, pts, 32, 0);
    }

    // Newest first: the beads still on their way in, then each strand from the clasp out to the left edge.
    int strung[THREADS] = {0}, col[THREADS] = {0}, full = 0;
    float together[THREADS];
    for (int k = 0; k < THREADS; k++) together[k] = 1e9f;
    Rgbf white = palette_ramp(f->palette, 1.0f);
    for (int i = events_after(s, f->t + (float)(g->cols - clasp) / speed) - 1; i >= 0 && full < b->count; i--) {
        const ScoreEvent *e = &s->events[i];
        int k = bead_thread(b, e);
        // Notes struck together on one thread share a bead.
        if (k < 0 || col[k] < -1 || together[k] - e->time < row_time * 0.5f) continue;
        together[k] = e->time;
        float wait = e->time - f->t, level, flash = 0;
        if (wait > 0) {
            col[k] = clasp + 2 + (int)(wait * speed);
            level = 0.45f + 0.4f * (1 - saturate(wait / 3.0f));
        } else {
            col[k] = clasp - 2 * strung[k]++;
            level = 0.95f - 0.5f * (float)(clasp - col[k]) / (float)clasp;
            flash = mexp(wait / 0.15f) * frame_bright(f);
        }
        if (col[k] < -1) {
            full++;
            continue;
        }
        Rgbf color = frame_dim(f, palette_accent(f->palette, k), level * (0.7f + 0.3f * e->velocity));
        float x = ((float)(col[k] + 1) / (float)g->cols * 2 - 1) * g->aspect;
        int row = cell_row(g, thread_y(b, f, &plucks, k, (x / reach + 1) * 0.5f));
        put_cell(g, col[k], row, BEADS[e->role].bead[0], rgbf_mix(color, white, flash));
        put_cell(g, col[k] + 1, row, BEADS[e->role].bead[1], rgbf_mix(color, white, flash));
    }

    // A small band gets fainter threads strung farther back, their beads drifting slowly, so the room still fills.
    Rgbf far = frame_dim(f, palette_ramp(f->palette, 0.5f), 0.4f);
    int drift = (int)(f->t * 1.5f * frame_pace(f));
    for (int k = 0; k <= b->count && b->count <= 4; k++)
        for (int c = 0; c < g->cols; c++) {
            float u = ((float)c + 0.5f) / (float)g->cols;
            float y = 0.9f - 1.8f * (float)k / (float)b->count - 0.12f * 4 * u * (1 - u);
            int bead = hash_u32(b->salt ^ (uint32_t)(k * 977 + c + drift)) % 13 == 0;
            if (bead || (c & 1)) put_cell_under(g, c, cell_row(g, y), bead ? 'o' : '.', far);
        }
    f->fx->glow = 0.5f;
}

/* ---------- Crowd ---------- */

typedef struct {
    float swing, sweep;
    uint32_t salt;
} Crowd;

void crowd_setup(void *state, Rng *r, const Frame *f) {
    Crowd *c = state;
    c->swing = rng_between(r, 0.1f, 0.2f);
    c->sweep = rng_between(r, 0.8f, 1.4f);
    c->salt = rng_u32(r);
    (void)f;
}

// A stick figure three cells wide, its head at (x + 1, y): hands up or down, and legs where its feet show.
static void stick(Grid *g, int x, int y, int up, int legs, Rgbf color) {
    put_cell(g, x + 1, y, 'o', color);
    put_cell(g, x + 1, y + 1, '|', color);
    put_cell(g, x, y + !up, up ? '\\' : '/', color);
    put_cell(g, x + 2, y + !up, up ? '/' : '\\', color);
    if (!legs) return;
    put_cell(g, x, y + 2, '/', color);
    put_cell(g, x + 2, y + 2, '\\', color);
}

// A show seen from the back of the room. The crowd holds up a few lights while the music is quiet, puts more hands
// up as it lifts, and pumps them and jumps on the kick once it drops. On stage, each of the band raises their hands
// with their own part, under lights that each flash with one too.
void crowd_draw(void *state, const Frame *f) {
    Crowd *c = state;
    Grid *g = f->grid;
    float energy = score_energy(f->score, f->t), kick = frame_pulse(f, ROLE_KICK, 0.14f, 1.0f);
    float hands = smoothstep(0.3f, 0.85f, energy), lights = 0.16f * smoothstep(0.5f, 0.3f, energy);
    int floor_row = g->rows * 2 / 5, rows = (g->rows - floor_row) / 2, phrase = (int)(f->t / (f->score->beat * 2));
    Rgbf ink = palette_ramp(f->palette, 0.9f), white = palette_ramp(f->palette, 1.0f);
    for (int j = 0; j < rows; j++) {
        float near = rows > 1 ? (float)j / (float)(rows - 1) : 1;
        for (int i = -1; i <= g->cols / 4; i++) {
            uint32_t id = hash_u32(c->salt ^ (uint32_t)(j * 131 + i) * 2654435761u);
            int x = i * 4 + (j & 1) * 2 + (int)(id & 1), y = floor_row + j * 2;
            int keen = hash01(id + 1) < hands, holds = hash01(id + 2) < 0.3f;
            // Before a drop the keen ones raise their hands a phrase at a time; in one, hands pump with the kick.
            int up = keen && (energy > 0.6f ? holds || kick > 0.25f : hash01(id + (uint32_t)phrase * 7919u) < 0.6f);
            int jump = keen && energy > 0.75f && kick > 0.55f && !f->still;
            Rgbf tint = rgbf_mix(ink, palette_accent(f->palette, (int)(id >> 8 & 3)), up ? 0.5f : 0.15f);
            Rgbf color = frame_dim(f, tint, 0.4f + 0.6f * near);
            y -= jump;
            stick(g, x, y, up, jump, color);
            if (hash01(id + 3) < lights) {
                float phase = hash01(id + 4) * TAU_F;
                int sway = (int)(msin(f->t * 1.3f * frame_pace(f) + phase) * 1.4f);
                float twinkle = 0.5f + 0.5f * msin(f->t * 5.0f + phase * 3);
                put_cell(g, x + 1 + sway, y - 1, '*', rgbf_mix(palette_accent(f->palette, 0), white, twinkle));
            }
        }
    }

    // The truss, with the name of tonight's act on it.
    Rgbf steel = palette_ramp(f->palette, 0.45f);
    for (int col = 0; col < g->cols; col++) put_cell(g, col, 0, col % 6 ? '=' : '#', steel);
    char sign[SEED_MAX + 8];
    snprintf(sign, sizeof sign, "[ %s ]", f->score->info->title);
    for (char *p = sign; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    text_at(g, (g->cols - (int)strlen(sign)) / 2, 0, sign, ink);

    // The stage: a lip between two speaker stacks that thump with the bass, and the band.
    int lip = floor_row - 2, wing = g->cols / 8;
    const char *speaker = frame_pulse(f, ROLE_BASS, 0.2f, 1.0f) > 0.4f ? "[O]" : "[o]";
    for (int col = wing; col < g->cols - wing; col++) put_cell(g, col, lip, '=', steel);
    for (int row = lip - 3; row < lip; row++) {
        text_at(g, wing, row, speaker, steel);
        text_at(g, g->cols - wing - 3, row, speaker, steel);
    }
    static const Role BAND[4] = {ROLE_BASS, ROLE_LEAD, ROLE_KICK, ROLE_ARP};
    for (int i = 0; i < 4; i++)
        stick(g, wing + 3 + (2 * i + 1) * (g->cols - 2 * wing - 9) / 8, lip - 3,
              frame_pulse(f, BAND[i], 0.2f, 1.0f) > 0.4f, 1, ink);

    static const Role LIGHTS[6] = {ROLE_KICK, ROLE_LEAD, ROLE_SNARE, ROLE_BASS, ROLE_HAT, ROLE_ARP};
    int lamps = g->cols / 14 < 3 ? 3 : g->cols / 14, span = g->rows - 3;
    float travel = frame_travel(f, 0.5f, 1.0f);
    for (int i = 0; i < lamps; i++) {
        int lamp = (2 * i + 1) * g->cols / (2 * lamps);
        float level = 0.3f + 0.7f * mmin(1.0f, frame_pulse(f, LIGHTS[i % 6], 0.2f, 1.0f));
        float lean = c->swing * (float)g->cols * msin(travel * c->sweep + (float)i * 1.9f) / (float)span;
        Rgbf light = palette_accent(f->palette, i);
        text_at(g, lamp - 1, 1, level > 0.6f ? "(@)" : "(o)", rgbf_mix(steel, light, level));
        // Each beam widens toward the floor, and shows only where nobody stands in front of it.
        for (int row = 2; row < g->rows; row++) {
            float u = (float)(row - 2) / (float)span, center = (float)lamp + lean * (float)(row - 2);
            float half = 0.8f + 4.0f * u;
            for (int col = mfloor_i(center - half) + 1; (float)col <= center + half; col++) {
                float off = mabs((float)col - center), fade = level * (1 - off / (half + 0.5f)) * (1 - 0.55f * u);
                int glyph = off >= 0.5f ? ':' : mabs(lean) < 0.3f ? '|' : lean > 0 ? '\\' : '/';
                put_cell_under(g, col, row, glyph, frame_dim(f, light, fade));
            }
        }
    }
    f->fx->glow = 0.45f + 0.2f * mmin(1.0f, kick);
}

/* ---------- Sea chart ---------- */

typedef struct {
    float swell, phase;
    uint32_t salt;
} Chart;

void chart_setup(void *state, Rng *r, const Frame *f) {
    Chart *c = state;
    c->swell = rng_between(r, 0.7f, 1.2f);
    c->phase = rng_between(r, 0, TAU_F);
    c->salt = rng_u32(r);
    (void)f;
}

// The column the voyage has reached at a time into the song.
static int chart_col(const Frame *f, float time) {
    return 6 + (int)(saturate(time / f->score->length) * (float)(f->grid->cols - 13));
}

// How far down the chart the course runs at a column, in rows: a slow swell that leaves room for an island above
// it and below.
static float chart_course(const Chart *c, const Grid *g, int col) {
    float swing = mmin((float)g->rows * 0.2f, (float)(g->rows - 17) * 0.5f);
    return (float)(g->rows / 2) + swing * msin((float)col / (float)g->cols * TAU_F * c->swell + c->phase);
}

static int chart_row(const Chart *c, const Grid *g, int col) {
    return mfloor_i(chart_course(c, g, col) + 0.5f);
}

// What stands on the island of each kind of section, in SectionKind's order: a lighthouse to set out from, palms, a
// peak, a volcano, an atoll, and a house to come home to.
static const char *const ISLANDS[SECTION_KINDS][3] = {
    {"  (*)  ", " _|=|_ ", "(_____)"},
    {"  \\|/  ", " __|__ ", "(_____)"},
    {"   /\\  ", " _/  \\_", "(_____)"},
    {"  ':'  ", " _/^\\_ ", "(_____)"},
    {"       ", " .-~-. ", "(_~~~_)"},
    {"   _   ", " _/#\\_ ", "(_____)"},
};

// The whole song as a sea chart: an island for each of its parts, set along the course in the order they play, and a
// ship that is always exactly as far across the chart as the song is through.
void chart_draw(void *state, const Frame *f) {
    Chart *c = state;
    Grid *g = f->grid;
    const SongInfo *info = f->score->info;
    Rgbf ink = palette_ramp(f->palette, 0.9f), faint = palette_ramp(f->palette, 0.45f);
    Rgbf land = palette_accent(f->palette, 1), sea = palette_ramp(f->palette, 0.5f);
    for (int col = 0; col < g->cols; col++) {
        put_cell(g, col, 0, col % 10 ? '-' : '+', faint);
        put_cell(g, col, g->rows - 1, col % 10 ? '-' : '+', faint);
    }
    for (int row = 1; row < g->rows - 1; row++) {
        put_cell(g, 0, row, row % 5 ? '|' : '+', faint);
        put_cell(g, g->cols - 1, row, row % 5 ? '|' : '+', faint);
    }
    char name[SEED_MAX + 20];
    snprintf(name, sizeof name, "[ a chart of %s ]", info->title);
    text_at(g, (g->cols - (int)strlen(name)) / 2, 0, name, ink);

    const Section *here = score_section(f->score, f->t);
    for (int i = 0; i < info->section_count; i++) {
        const Section *sec = &info->sections[i];
        // Islands take turns above and below the course, so neighbors never share a shore.
        int col = chart_col(f, (float)sec->start), side = i & 1 ? 1 : -1, row = chart_row(c, g, col) + side * 5;
        Rgbf color = sec == here ? land : frame_dim(f, land, 0.6f);
        for (int line = 0; line < 3; line++)
            for (int k = 0; k < 7; k++)
                if (ISLANDS[sec->kind][line][k] != ' ')
                    put_cell(g, col - 3 + k, row - 1 + line, ISLANDS[sec->kind][line][k], color);
        const char *label = section_names[sec->kind];
        text_at(g, col - (int)strlen(label) / 2, row + side * 2, label, sec == here ? ink : faint);
    }

    // The course: inked in where the ship has been, dotted where it has yet to go, with an X where the song ends.
    int port = chart_col(f, 0), ship = chart_col(f, f->t), end = chart_col(f, f->score->length), sailed = 0;
    StrokePoint wake[64];
    for (int col = port; sailed < 64; col += 3) {
        int at = col < ship ? col : ship;
        wake[sailed++] = (StrokePoint){(((float)at + 0.5f) / (float)g->cols * 2 - 1) * g->aspect,
                                       1 - (chart_course(c, g, at) + 0.5f) / (float)g->rows * 2, ink};
        if (at == ship) break;
    }
    if (sailed > 1) stroke_draw(f->stroker, wake, sailed, 0);
    for (int col = ship + 2; col < end; col += 2) put_cell(g, col, chart_row(c, g, col), '.', faint);
    put_cell(g, end, chart_row(c, g, end), 'X', palette_accent(f->palette, 0));
    static const char *const SHIP[3] = {"  |\\  ", " _|_\\_", " \\___/"};
    for (int line = 0; line < 3; line++)
        for (int k = 0; k < 6; k++)
            if (SHIP[line][k] != ' ')
                put_cell(g, ship - 3 + k, chart_row(c, g, ship) - 2 + line, SHIP[line][k],
                         palette_ramp(f->palette, 1.0f));

    // Open water, behind everything: a sea serpent that crosses now and then, a compass rose, and waves.
    int head = (int)(f->t * 2.0f * frame_pace(f)) % (g->cols * 2) - 10;
    for (int k = 0; k < 10; k++) {
        int at = head - k, glyph = k == 0 ? '>' : k == 1 ? 'o' : "_/\\"[((at % 3) + 3) % 3];
        put_cell_under(g, at, g->rows - 4, glyph, palette_accent(f->palette, 2));
    }
    static const char *const ROSE[5] = {"   N   ", "   |   ", "W--+--E", "   |   ", "   S   "};
    for (int line = 0; line < 5; line++)
        for (int k = 0; k < 7; k++)
            if (ROSE[line][k] != ' ') put_cell_under(g, 3 + k, g->rows - 7 + line, ROSE[line][k], faint);
    for (int row = 1; row < g->rows - 1; row++)
        for (int col = 1; col < g->cols - 1; col++) {
            uint32_t id = hash_u32(c->salt ^ (uint32_t)(row * 257 + col));
            if (id % 23) continue;
            float shimmer = 0.5f + 0.5f * msin(f->t * 1.1f * frame_pace(f) + hash01(id) * TAU_F);
            put_cell_under(g, col, row, shimmer > 0.5f ? '~' : '-', frame_dim(f, sea, 0.45f + 0.3f * shimmer));
        }
    f->fx->glow = 0.3f;
}

/* ---------- Life ---------- */

#define LIFE_ROWS 48

typedef struct {
    float density;
    int mirror;
    uint32_t salt;
} Life;

void life_setup(void *state, Rng *r, const Frame *f) {
    Life *l = state;
    l->density = rng_between(r, 0.14f, 0.3f);
    // Bit 0 mirrors the soup left to right, bit 1 top to bottom. Most soups get both, and grow like snowflakes.
    l->mirror = rng_chance(r, 60) ? 3 : rng_range(r, 0, 3);
    l->salt = rng_u32(r);
    (void)f;
}

// Brings a cell to life along with its reflections, so a mirrored board stays mirrored.
static void life_set(uint64_t *board, int w, int h, int mirror, int x, int y) {
    x = ((x % w) + w) % w, y = ((y % h) + h) % h;
    for (int m = 0; m < 4; m++)
        if (!(m & ~mirror)) board[m & 2 ? h - 1 - y : y] |= 1ull << (m & 1 ? w - 1 - x : x);
}

// One generation on a board that wraps at every edge, a whole row at a time: the neighbors of all its cells are
// counted at once as bit planes of ones and twos.
static void life_step(const uint64_t *now, uint64_t *next, int w, int h) {
    uint64_t mask = w == 64 ? ~0ull : (1ull << w) - 1;
    for (int y = 0; y < h; y++) {
        uint64_t rows[3] = {now[(y + h - 1) % h], now[y], now[(y + 1) % h]}, ones[3], twos[3];
        for (int k = 0; k < 3; k++) {
            uint64_t left = ((rows[k] << 1) | (rows[k] >> (w - 1))) & mask;
            uint64_t right = (rows[k] >> 1) | ((rows[k] << (w - 1)) & mask), self = k == 1 ? 0 : rows[k];
            ones[k] = left ^ right ^ self;
            twos[k] = (left & right) | (self & (left ^ right));
        }
        uint64_t one = ones[0] ^ ones[1] ^ ones[2], carry = (ones[0] & ones[1]) | (ones[2] & (ones[0] ^ ones[1]));
        // Exactly one two among the four means two or three neighbors, which the ones plane tells apart.
        uint64_t single = (twos[0] ^ twos[1]) ^ (twos[2] ^ carry), more = (twos[0] & twos[1]) | (twos[2] & carry);
        next[y] = single & ~more & (one | now[y]) & mask;
    }
}

// A glider and an r-pentomino, as the x and y of each cell.
static const int8_t LIFE_SHAPES[2][5][2] = {{{1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}},
                                            {{1, 0}, {2, 0}, {0, 1}, {1, 1}, {1, 2}}};

// Conway's game of life, a generation a sixteenth, on a board two characters to a cell. The soup it grows from is
// usually mirrored, and every snare drops a glider or an r-pentomino into it. Played again from the start each frame,
// so the board is the same whenever a moment is drawn.
void life_draw(void *state, const Frame *f) {
    Life *l = state;
    Grid *g = f->grid;
    int w = g->cols / 2 < 64 ? g->cols / 2 : 64, h = g->rows - 2 < LIFE_ROWS ? g->rows - 2 : LIFE_ROWS;
    uint64_t boards[3][LIFE_ROWS] = {{0}}, *before = boards[0], *now = boards[1], *next = boards[2];
    for (int y = 0; y < (l->mirror & 2 ? (h + 1) / 2 : h); y++)
        for (int x = 0; x < (l->mirror & 1 ? (w + 1) / 2 : w); x++)
            if (hash01(l->salt ^ (uint32_t)(y * 64 + x) * 2654435761u) < l->density)
                life_set(now, w, h, l->mirror, x, y);

    float tick = f->score->beat / 4;
    // A board turning over is a field of blinking cells, so for calmer pictures it does so at most four times a second.
    while (tick < (f->calm ? 0.25f : 0.1f)) tick *= 2;
    tick /= frame_pace(f);
    float age = mmin(f->local / tick, 400.0f), start = f->t - f->local;
    for (int n = 0; n < (int)age; n++) {
        life_step(now, next, w, h);
        uint64_t *spare = before;
        before = now, now = next, next = spare;
        float from = start + (float)n * tick;
        if (f->react <= 0 || !score_count(f->score, ROLE_SNARE, from, from + tick)) continue;
        uint32_t id = hash_u32(l->salt + (uint32_t)n * 2654435761u);
        for (int k = 0; k < 5; k++)
            life_set(now, w, h, l->mirror, (int)(id % (uint32_t)w) + LIFE_SHAPES[id >> 31][k][0],
                     (int)(id / 64 % (uint32_t)h) + LIFE_SHAPES[id >> 31][k][1]);
    }
    life_step(now, next, w, h);

    // Cells born this generation arrive in an accent color, the ones about to die dim out, and the ones about to be
    // born show as dots for the second half of the tick.
    float phase = mfract(age);
    int left = (g->cols - w * 2) / 2, cells = 0;
    Rgbf settled = frame_dim(f, palette_ramp(f->palette, 0.8f), frame_contrast(f));
    Rgbf fresh = f->calm ? settled : palette_accent(f->palette, 0);
    Rgbf coming = frame_dim(f, palette_accent(f->palette, 1), 0.5f);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint64_t bit = 1ull << x;
            int alive = (now[y] & bit) != 0, stays = (next[y] & bit) != 0;
            if (!alive && (!stays || phase < 0.5f)) continue;
            Rgbf color = coming;
            if (alive) {
                cells++;
                color = before[y] & bit ? settled : rgbf_mix(fresh, settled, phase);
                if (!stays) color = frame_dim(f, color, 1 - 0.7f * phase);
            }
            put_cell(g, left + x * 2, y + 2, alive ? '[' : '.', color);
            put_cell(g, left + x * 2 + 1, y + 2, alive ? ']' : '.', color);
        }
    char text[48];
    snprintf(text, sizeof text, "LIFE   GEN %04d   CELLS %d", (int)age, cells);
    text_at(g, left, 0, text, palette_ramp(f->palette, 0.95f));
    f->fx->glow = 0.4f + 0.2f * frame_pulse(f, ROLE_KICK, 0.15f, 1.0f);
}
