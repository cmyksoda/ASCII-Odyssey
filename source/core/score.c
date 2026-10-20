// SPDX-License-Identifier: GPL-3.0-only
#include "score.h"

#include "mathx.h"
#include "player.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Score *score;
    int capacity;
} Collector;

static void collect(void *context, double seconds, int channel, int note, float velocity, const Instrument *ins) {
    Collector *c = context;
    Score *s = c->score;
    if (s->count == c->capacity) {
        int cap = c->capacity ? c->capacity * 2 : 4096;
        ScoreEvent *grown = realloc(s->events, sizeof(ScoreEvent) * (size_t)cap);
        if (!grown) return;
        s->events = grown;
        c->capacity = cap;
    }
    s->events[s->count++] = (ScoreEvent){(float)seconds, velocity, (uint8_t)ins->role, (uint8_t)note, (uint8_t)channel};
}

int score_build(Score *s, const Composition *c) {
    memset(s, 0, sizeof *s);
    s->info = &c->info;
    s->beat = 60.0f / (float)c->info.bpm;
    Collector collector = {s, 0};
    Player p;
    if (player_init(&p, c->song, 1) != 0) return -1;
    p.listener = collect;
    p.listener_context = &collector;
    static float scratch[2 * 4096];
    while (player_render(&p, scratch, 4096) == 4096) {}
    s->length = (float)player_seconds(&p);
    player_free(&p);
    for (int i = 0; i < s->count; i++) s->role_count[s->events[i].role]++;
    for (int r = 0; r < ROLE_COUNT; r++) {
        s->by_role[r] = malloc(sizeof(int) * (size_t)(s->role_count[r] + 1));
        if (!s->by_role[r]) return -1;
        s->role_count[r] = 0;
    }
    for (int i = 0; i < s->count; i++) {
        int r = s->events[i].role;
        s->by_role[r][s->role_count[r]++] = i;
    }
    for (int r = 0; r < ROLE_COUNT; r++) {
        s->velocity_sum[r] = malloc(sizeof(float) * (size_t)(s->role_count[r] + 1));
        if (!s->velocity_sum[r]) return -1;
        s->velocity_sum[r][0] = 0;
        for (int k = 0; k < s->role_count[r]; k++)
            s->velocity_sum[r][k + 1] = s->velocity_sum[r][k] + s->events[s->by_role[r][k]].velocity;
    }
    s->area_count = (int)(s->length * 10) + 2;
    s->energy_area = malloc(sizeof(float) * (size_t)s->area_count);
    if (!s->energy_area) return -1;
    s->energy_area[0] = 0;
    for (int i = 1; i < s->area_count; i++) {
        float t = (float)i / 10;
        s->energy_area[i] = s->energy_area[i - 1] + 0.05f * (score_energy(s, t - 0.1f) + score_energy(s, t));
    }
    return 0;
}

float score_energy_area(const Score *s, float t) {
    if (!s->energy_area || t <= 0) return 0;
    float x = t * 10;
    int i = mfloor_i(x);
    if (i >= s->area_count - 1) return s->energy_area[s->area_count - 1];
    return s->energy_area[i] + (s->energy_area[i + 1] - s->energy_area[i]) * (x - (float)i);
}

void score_free(Score *s) {
    free(s->events);
    free(s->energy_area);
    for (int r = 0; r < ROLE_COUNT; r++) {
        free(s->by_role[r]);
        free(s->velocity_sum[r]);
    }
    memset(s, 0, sizeof *s);
}

// Index into by_role of the last event at or before t, or -1.
static int last_index(const Score *s, Role role, float t) {
    const int *idx = s->by_role[role];
    int lo = 0, hi = s->role_count[role] - 1, found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s->events[idx[mid]].time <= t) {
            found = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return found;
}

const ScoreEvent *score_last(const Score *s, Role role, float t) {
    int i = last_index(s, role, t);
    return i < 0 ? NULL : &s->events[s->by_role[role][i]];
}

float score_since(const Score *s, Role role, float t) {
    const ScoreEvent *e = score_last(s, role, t);
    return e ? t - e->time : 1e6f;
}

float score_pulse(const Score *s, Role role, float t, float tau) {
    const ScoreEvent *e = score_last(s, role, t);
    if (!e) return 0;
    float dt = t - e->time;
    return dt > tau * 8 ? 0 : e->velocity * mexp(-dt / tau);
}

float score_pulse_area(const Score *s, Role role, float t, float tau) {
    int last = last_index(s, role, t);
    if (last < 0 || !s->velocity_sum[role]) return 0;
    // Notes older than eight time constants have given all they will; only the recent ones are still unfolding.
    int k = last;
    while (k >= 0 && s->events[s->by_role[role][k]].time > t - tau * 8) k--;
    float area = s->velocity_sum[role][k + 1] * tau;
    for (int i = k + 1; i <= last; i++) {
        const ScoreEvent *e = &s->events[s->by_role[role][i]];
        area += e->velocity * tau * (1 - mexp(-(t - e->time) / tau));
    }
    return area;
}

int score_count(const Score *s, Role role, float from, float to) {
    return last_index(s, role, to) - last_index(s, role, from);
}

const Section *score_section(const Score *s, float t) {
    const SongInfo *info = s->info;
    for (int i = info->section_count - 1; i >= 0; i--)
        if (t >= info->sections[i].start) return &info->sections[i];
    return &info->sections[0];
}

const ChordMark *score_chord(const Score *s, float t) {
    const SongInfo *info = s->info;
    for (int i = info->chord_count - 1; i >= 0; i--)
        if (t >= info->chords[i].time) return &info->chords[i];
    return info->chord_count ? &info->chords[0] : NULL;
}

float score_energy(const Score *s, float t) {
    const SongInfo *info = s->info;
    const Section *now = score_section(s, t);
    int i = (int)(now - info->sections);
    float e = now->energy;
    // Ease in over the first beat of a section, so energy-driven motion never jumps.
    if (i > 0) {
        float k = smoothstep(0.0f, s->beat, t - (float)now->start);
        e = mixf(info->sections[i - 1].energy, e, k);
    }
    if (t > info->length) e *= mmax(0, 1 - (t - (float)info->length) / 4);
    return e;
}
