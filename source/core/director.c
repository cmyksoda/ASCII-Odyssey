// SPDX-License-Identifier: GPL-3.0-only
#include "director.h"

#include "mathx.h"

#include <string.h>

#define CAST 5

static int fits(const SceneKind *k, Mood mood, float energy) {
    return (k->moods & MOOD_BIT(mood)) && energy >= k->energy_lo - 0.05f && energy <= k->energy_hi + 0.05f;
}

static int choose(Rng *r, const int *cast, int cast_count, Mood mood, float energy, int avoid) {
    int options[CAST], n = 0;
    for (int i = 0; i < cast_count; i++)
        if (cast[i] != avoid && fits(&scene_kinds[cast[i]], mood, energy)) options[n++] = cast[i];
    if (!n)
        for (int i = 0; i < cast_count; i++)
            if (cast[i] != avoid) options[n++] = cast[i];
    return n ? options[rng_range(r, 0, n)] : cast[0];
}

void direction_plan(Direction *d, const Score *score, Rng *r, const Frame *proto) {
    const SongInfo *info = score->info;
    Mood mood = info->mood;
    memset(d, 0, sizeof *d);
    // A piece keeps to a cast of scenes, so it feels like one voyage rather than a channel-surf.
    int cast[CAST], cast_count = 0, pool[64], pool_count = 0;
    for (int k = 0; k < scene_kind_count && pool_count < 64; k++)
        if (scene_kinds[k].moods & MOOD_BIT(mood)) pool[pool_count++] = k;
    while (cast_count < CAST && pool_count > 0) {
        int i = rng_range(r, 0, pool_count);
        cast[cast_count++] = pool[i];
        pool[i] = pool[--pool_count];
    }
    if (!cast_count) cast[cast_count++] = 0;
    int previous = -1;
    float bar = (float)info->bar_seconds;
    for (int s = 0; s < info->section_count && d->count < MAX_SHOTS; s++) {
        const Section *sec = &info->sections[s];
        // Long sections change shots every eight bars; drops do it on the bar line with a cut.
        int pieces = sec->bars >= 16 && sec->kind != SECTION_BREAK ? sec->bars / 8 : 1;
        for (int p = 0; p < pieces && d->count < MAX_SHOTS; p++) {
            Shot *shot = &d->shots[d->count];
            shot->start = (float)sec->start + (float)(p * (sec->bars / pieces)) * bar;
            shot->end = p + 1 == pieces ? (float)sec->end : shot->start + (float)(sec->bars / pieces) * bar;
            shot->kind = choose(r, cast, cast_count, mood, sec->energy, previous);
            if (sec->kind == SECTION_DROP && p == 0) {
                shot->entrance = rng_chance(r, 60) ? ENTER_CUT : ENTER_RADIAL;
                shot->fade = shot->entrance == ENTER_CUT ? 0 : score->beat;
            } else {
                static const Entrance soft[3] = {ENTER_DISSOLVE, ENTER_WIPE, ENTER_DECODE};
                shot->entrance = soft[rng_range(r, 0, 3)];
                shot->fade = score->beat * (sec->energy < 0.4f ? 4.0f : 2.0f);
            }
            Rng shot_rng;
            rng_init(&shot_rng, rng_next(r), "shot");
            scene_kinds[shot->kind].setup(shot->state, &shot_rng, proto);
            previous = shot->kind;
            d->count++;
        }
    }
    if (d->count) d->shots[d->count - 1].end = score->length + 10;
}

static void render_shot(Shot *shot, Frame *f, Grid *into) {
    Grid *keep = f->grid;
    Stroker *st = f->stroker;
    Grid *stroke_grid = st->grid;
    f->grid = into;
    st->grid = into;
    f->local = f->t - shot->start;
    f->length = shot->end - shot->start;
    grid_clear(into);
    scene_kinds[shot->kind].draw(shot->state, f);
    f->grid = keep;
    st->grid = stroke_grid;
}

static const char SCRAMBLE[] = "!#$%&*+-/:;<=>?@[]^_{|}~";

static void blend(Grid *from, Grid *to, Entrance how, float progress, float t) {
    float cx = (float)to->cols / 2, cy = (float)to->rows / 2;
    float reach = msqrt(cx * cx + cy * cy * 4);
    for (int row = 0; row < to->rows; row++)
        for (int col = 0; col < to->cols; col++) {
            float h = hash01((uint32_t)(row * 7919 + col * 104729));
            float key;
            switch (how) {
            case ENTER_WIPE: key = (float)col / (float)to->cols * 0.9f + h * 0.1f; break;
            case ENTER_RADIAL: key = mhypot((float)col - cx, ((float)row - cy) * 2) / reach * 0.9f + h * 0.1f; break;
            default: key = h; break;
            }
            Cell *dst = grid_at(to, col, row);
            if (key > progress) {
                *dst = *grid_at(from, col, row);
            } else if (how == ENTER_DECODE && key > progress - 0.12f && dst->glyph != ' ') {
                // Cells about to settle flicker through random symbols first, like a signal locking on.
                uint32_t roll = hash_u32((uint32_t)(row * 131 + col) ^ (uint32_t)(t * 24.0f));
                dst->glyph = (uint8_t)SCRAMBLE[roll % (sizeof SCRAMBLE - 1)];
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
        render_shot(&d->shots[i - 1], f, scratch);
        float glow = f->fx->glow;
        render_shot(shot, f, f->grid);
        float progress = smoothstep(0.0f, shot->fade, since);
        f->fx->glow = mixf(glow, f->fx->glow, progress);
        blend(scratch, f->grid, shot->entrance, progress * 1.05f, f->t);
    } else {
        render_shot(shot, f, f->grid);
    }
}
