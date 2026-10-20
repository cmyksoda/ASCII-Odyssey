// SPDX-License-Identifier: GPL-3.0-only
#include "director.h"

#include "mathx.h"

#include <string.h>

const char *const entrance_names[ENTRANCES] = {"cut",   "dissolve", "wipe",   "diagonal", "radial", "iris",
                                               "blinds", "checker", "spiral", "melt",     "decode", "fade"};

static const Entrance SOFT[] = {ENTER_DISSOLVE, ENTER_WIPE,   ENTER_DIAGONAL, ENTER_RADIAL, ENTER_IRIS, ENTER_BLINDS,
                                ENTER_CHECKER,  ENTER_SPIRAL, ENTER_MELT,     ENTER_DECODE, ENTER_FADE};
// A drop lands with a burst that opens from the middle or sweeps across in half a beat.
static const Entrance BURST[] = {ENTER_RADIAL, ENTER_IRIS, ENTER_CHECKER, ENTER_BLINDS, ENTER_DIAGONAL};

static int edm(Mood m) {
    return m == MOOD_SYNTHWAVE || m == MOOD_TRANCE || m == MOOD_KEYGEN || m == MOOD_JUNGLE || m == MOOD_DUBSTEP;
}

// How many shots a section gets: each a few bars long, from about seven and a half seconds in a quiet passage down to
// five and a half in a drop.
static int shot_count(const Section *sec, float bar_seconds) {
    float target = 8.0f - 2.5f * sec->energy, length = (float)sec->bars * bar_seconds;
    int n = (int)(length / target + 0.5f);
    return n < 1 ? 1 : n > sec->bars ? sec->bars : n;
}

// The scene whose intensity best suits the music, among those not shown yet. Drops lean on reactive scenes and quiet
// stretches on still ones. Once every reactive (or still) scene made for the mood has had a turn, reactive (or still)
// scenes from other moods stand in before any comes back, except those made for one mood alone.
static int choose_scene(Rng *r, Mood mood, const Section *sec, const int *uses, int previous) {
    int fresh[2] = {0, 0};
    for (int k = 0; k < scene_kind_count; k++)
        if ((scene_kinds[k].moods & MOOD_BIT(mood)) && !uses[k]) fresh[scene_kinds[k].reactive != 0]++;
    int best = -1;
    float best_cost = 1e9f;
    for (int k = 0; k < scene_kind_count; k++) {
        const SceneKind *s = &scene_kinds[k];
        int own = (s->moods & MOOD_BIT(mood)) != 0, single = !(s->moods & (s->moods - 1));
        if (k == previous || !s->moods || (!own && (fresh[s->reactive != 0] || single))) continue;
        float cost = mabs(s->intensity - sec->energy) * (own ? 1.5f : 3.0f) + rng_float(r) * 0.5f + (float)uses[k] * 3.0f +
                     (own ? 0.0f : 0.5f);
        if (sec->kind == SECTION_DROP && !s->reactive) cost += 1.2f;
        if (sec->energy < 0.4f && s->reactive) cost += 0.3f;
        if (cost < best_cost) {
            best_cost = cost;
            best = k;
        }
    }
    return best >= 0 ? best : previous >= 0 ? previous : 0;
}

static Entrance pick_entrance(Rng *r, const Entrance *list, int count, Entrance previous) {
    Entrance e;
    do e = list[rng_range(r, 0, count)];
    while (e == previous && count > 1);
    return e;
}

static int palette_for(SectionKind kind) {
    switch (kind) {
    case SECTION_DROP: return 1;
    case SECTION_BUILD:
    case SECTION_BREAK: return 2;
    default: return 0;
    }
}

void direction_plan(Direction *d, const Score *score, Rng *r, const Frame *proto, const Palette *const palettes[3]) {
    const SongInfo *info = score->info;
    Mood mood = info->mood;
    memset(d, 0, sizeof *d);
    d->scratch_shot = -1;
    for (int i = 0; i < 3; i++) d->palettes[i] = palettes[i];
    int uses[64] = {0}, previous = -1;
    Entrance last = ENTER_CUT;
    float bar = (float)info->bar_seconds;
    for (int s = 0; s < info->section_count && d->count < MAX_SHOTS; s++) {
        const Section *sec = &info->sections[s];
        int pieces = shot_count(sec, bar);
        for (int p = 0; p < pieces && d->count < MAX_SHOTS; p++) {
            Shot *shot = &d->shots[d->count];
            shot->start = (float)sec->start + (float)(p * sec->bars / pieces) * bar;
            shot->end = (float)sec->start + (float)((p + 1) * sec->bars / pieces) * bar;
            shot->kind = choose_scene(r, mood, sec, uses, previous);
            shot->palette = palette_for(sec->kind);
            shot->direction = rng_range(r, 0, 4);
            if (sec->kind == SECTION_DROP && p == 0 && edm(mood)) {
                shot->entrance = pick_entrance(r, BURST, (int)(sizeof BURST / sizeof BURST[0]), last);
                shot->fade = score->beat * 0.5f;
            } else {
                shot->entrance = pick_entrance(r, SOFT, (int)(sizeof SOFT / sizeof SOFT[0]), last);
                shot->fade = mmin(score->beat * (sec->energy < 0.5f ? 4.0f : 2.0f), (shot->end - shot->start) * 0.4f);
            }
            last = shot->entrance;
            Frame look = *proto;
            look.palette = palettes[shot->palette];
            Rng shot_rng;
            rng_init(&shot_rng, rng_next(r), "shot");
            scene_kinds[shot->kind].setup(shot->state, &shot_rng, &look);
            uses[shot->kind]++;
            previous = shot->kind;
            d->count++;
        }
    }
    if (d->count) d->shots[d->count - 1].end = score->length + 10;
}

static void render_shot(const Direction *d, Shot *shot, Frame *f, Grid *into) {
    Grid *keep = f->grid;
    Stroker *st = f->stroker;
    Grid *stroke_grid = st->grid;
    f->grid = into;
    st->grid = into;
    f->local = f->t - shot->start;
    f->length = shot->end - shot->start;
    f->palette = d->palettes[shot->palette];
    // Each kind of scene either always answers the music, fully, or never does.
    f->react = d->by_section ? f->section_react : scene_kinds[shot->kind].reactive ? 1.0f : 0.0f;
    grid_clear(into);
    scene_kinds[shot->kind].draw(shot->state, f);
    f->grid = keep;
    st->grid = stroke_grid;
}

static const char SCRAMBLE[] = "!#$%&*+-/:;<=>?@[]^_{|}~";


// When a cell of the new shot takes over, as a fraction of the transition: each kind of entrance orders the cells its
// own way, with a little per-cell noise so no edge is ruler-straight.
static float reveal_key(Entrance how, int direction, int col, int row, const Grid *g) {
    float x = ((float)col + 0.5f) / (float)g->cols, y = ((float)row + 0.5f) / (float)g->rows;
    float h = hash01((uint32_t)(row * 7919 + col * 104729));
    float dx = (x - 0.5f) * g->aspect, dy = y - 0.5f, reach = 0.5f * msqrt(g->aspect * g->aspect + 1);
    switch (how) {
    case ENTER_WIPE: {
        float along = direction == 0 ? x : direction == 1 ? 1 - x : direction == 2 ? y : 1 - y;
        return along * 0.92f + h * 0.08f;
    }
    case ENTER_DIAGONAL: return ((direction & 1 ? 1 - x : x) + (direction & 2 ? 1 - y : y)) * 0.46f + h * 0.08f;
    case ENTER_RADIAL: return mhypot(dx, dy) / reach * 0.92f + h * 0.08f;
    case ENTER_IRIS: return (1 - mhypot(dx, dy) / reach) * 0.92f + h * 0.08f;
    case ENTER_BLINDS: return ((float)(row % 5) + 0.5f) / 5.0f * 0.9f + h * 0.1f;
    case ENTER_CHECKER: {
        int bx = col / 6, by = row / 3;
        return ((bx + by) & 1 ? 0.5f : 0.0f) + 0.45f * hash01((uint32_t)(bx * 31 + by * 977));
    }
    case ENTER_SPIRAL: {
        float a = (matan2(dy, dx) + PI_F) / TAU_F;
        return mfract(a + mhypot(dx, dy) / reach * 0.75f) * 0.9f + h * 0.1f;
    }
    case ENTER_MELT: return y * 0.55f + hash01((uint32_t)col * 2654435761u) * 0.4f + h * 0.05f;
    default: return h;
    }
}

// Moves a color away from the ground by k: toward it below 1, past itself above 1.
static Rgb from_ground(Rgb c, Rgb ground, float k) {
    Rgbf a = rgbf(ground), b = rgbf(c);
    return rgb_from_f((Rgbf){a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k});
}

static void blend(Grid *from, Grid *to, const Shot *shot, float progress, float t, Rgb ground) {
    if (shot->entrance == ENTER_FADE) {
        // Through the bare ground: the old shot fades out, then the new one comes up.
        for (int i = 0; i < to->cols * to->rows; i++) {
            if (progress < 0.5f) {
                to->cells[i] = from->cells[i];
                to->cells[i].fg = from_ground(to->cells[i].fg, ground, 1 - progress * 2);
            } else {
                to->cells[i].fg = from_ground(to->cells[i].fg, ground, progress * 2 - 1);
            }
        }
        return;
    }
    int edged = shot->entrance != ENTER_DISSOLVE && shot->entrance != ENTER_DECODE && shot->entrance != ENTER_CHECKER;
    for (int row = 0; row < to->rows; row++)
        for (int col = 0; col < to->cols; col++) {
            float key = reveal_key(shot->entrance, shot->direction, col, row, to);
            Cell *dst = grid_at(to, col, row);
            if (key > progress) {
                *dst = *grid_at(from, col, row);
            } else if (shot->entrance == ENTER_DECODE && key > progress - 0.12f && dst->glyph != ' ') {
                // Cells about to settle flicker through random symbols first, like a signal locking on.
                uint32_t roll = hash_u32((uint32_t)(row * 131 + col) ^ (uint32_t)(t * 24.0f));
                dst->glyph = (uint8_t)SCRAMBLE[roll % (sizeof SCRAMBLE - 1)];
            } else if (edged && key > progress - 0.035f && dst->glyph != ' ') {
                // The front of a sweep stands out as it passes: brighter on a dark ground, deeper on a light one.
                dst->fg = from_ground(dst->fg, ground, 1.6f);
            }
        }
}

void direction_draw(Direction *d, Frame *f, Grid *scratch) {
    if (!d->count) return;
    int i = 0;
    while (i + 1 < d->count && f->t >= d->shots[i + 1].start) i++;
    Shot *shot = &d->shots[i];
    float since = f->t - shot->start;
    if (i > 0 && shot->fade > 0 && since < shot->fade) {
        // The outgoing shot redraws at fifteen frames a second, so a transition costs the Wii little more than a shot.
        if (d->scratch_shot != i - 1 || f->t < d->scratch_t || f->t - d->scratch_t >= 1.0f / 15) {
            render_shot(d, &d->shots[i - 1], f, scratch);
            d->scratch_shot = i - 1;
            d->scratch_t = f->t;
            d->scratch_glow = f->fx->glow;
        }
        float glow = d->scratch_glow;
        render_shot(d, shot, f, f->grid);
        float progress = smoothstep(0.0f, shot->fade, since);
        f->fx->glow = mixf(glow, f->fx->glow, progress);
        f->fx->bg = rgb_mix(d->palettes[d->shots[i - 1].palette]->bg, d->palettes[shot->palette]->bg, progress);
        blend(scratch, f->grid, shot, progress * 1.05f, f->t, f->fx->bg);
    } else {
        render_shot(d, shot, f, f->grid);
        f->fx->bg = d->palettes[shot->palette]->bg;
    }
}
