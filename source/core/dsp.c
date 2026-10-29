// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp.h"

#include "mathx.h"
#include "song.h"

#include <stdlib.h>
#include <string.h>

void svf_reset(Svf *f) {
    f->ic1 = f->ic2 = 0;
}

void svf_set(Svf *f, float hz, float q) {
    if (hz > AUDIO_RATE * 0.45f) hz = AUDIO_RATE * 0.45f;
    if (hz < 10) hz = 10;
    float w = PI_F * hz / AUDIO_RATE;
    float g = msin(w) / mcos(w);
    f->k = 1 / (q < 0.5f ? 0.5f : q);
    f->a1 = 1 / (1 + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

float knob_hz(float knob) {
    return 30 * mexp2(clampf(knob, 0, 1) * 9.3f);
}

// Dattorro's delay lengths and output taps are given at 29761 Hz.
static int scaled(int samples) {
    return (int)((float)samples * (AUDIO_RATE / 29761.0f) + 0.5f);
}

static float *carve(float **memory, DelayLine *d, int length) {
    d->buf = *memory;
    d->size = length + 2;
    d->pos = 0;
    *memory += d->size;
    return d->buf;
}

static float tap(const DelayLine *d, int delay) {
    int i = d->pos - delay;
    if (i < 0) i += d->size;
    return d->buf[i];
}

static float tap_frac(const DelayLine *d, float delay) {
    int k = mfloor_i(delay);
    float f = delay - (float)k;
    return tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f;
}

static void push(DelayLine *d, float v) {
    d->buf[d->pos] = v;
    if (++d->pos == d->size) d->pos = 0;
}

static float allpass(DelayLine *d, int length, float g, float x) {
    float delayed = tap(d, length);
    float w = x - g * delayed;
    push(d, w);
    return delayed + g * w;
}

static const int IN_LENGTHS[4] = {142, 107, 379, 277};

int plate_init(Plate *p) {
    memset(p, 0, sizeof *p);
    int total = scaled(1200) + 4 * 2 + scaled(672 + 32) + scaled(908 + 32) + scaled(4453) + scaled(4217) +
                scaled(1800) + scaled(2656) + scaled(3720) + scaled(3163) + 40;
    for (int i = 0; i < 4; i++) total += scaled(IN_LENGTHS[i]);
    p->memory = calloc((size_t)total, sizeof(float));
    if (!p->memory) return -1;
    float *m = p->memory;
    carve(&m, &p->pre, scaled(1200));
    for (int i = 0; i < 4; i++) carve(&m, &p->in[i], scaled(IN_LENGTHS[i]));
    carve(&m, &p->ap_a, scaled(672 + 32));
    carve(&m, &p->del_a1, scaled(4453));
    carve(&m, &p->ap_a2, scaled(1800));
    carve(&m, &p->del_a2, scaled(3720));
    carve(&m, &p->ap_b, scaled(908 + 32));
    carve(&m, &p->del_b1, scaled(4217));
    carve(&m, &p->ap_b2, scaled(2656));
    carve(&m, &p->del_b2, scaled(3163));
    p->decay = 0.72f;
    p->damping = 0.35f;
    p->mix = 1;
    return 0;
}

void plate_free(Plate *p) {
    free(p->memory);
    p->memory = NULL;
}

void plate_process(Plate *p, float in, float *left, float *right) {
    const float excursion = 16 * (AUDIO_RATE / 29761.0f);
    push(&p->pre, in);
    float x = tap(&p->pre, scaled(600));
    p->bandwidth_state += 0.7f * (x - p->bandwidth_state);
    x = p->bandwidth_state;
    x = allpass(&p->in[0], scaled(142), 0.75f, x);
    x = allpass(&p->in[1], scaled(107), 0.75f, x);
    x = allpass(&p->in[2], scaled(379), 0.625f, x);
    x = allpass(&p->in[3], scaled(277), 0.625f, x);
    p->lfo += TAU_F * 1.0f / AUDIO_RATE;
    if (p->lfo > TAU_F) p->lfo -= TAU_F;
    float mod_a = msin(p->lfo) * excursion, mod_b = mcos(p->lfo) * excursion;

    // Left half of the figure eight, fed by the right half's tail.
    float a = x + p->decay * p->feedback_b;
    float delayed = tap_frac(&p->ap_a, (float)scaled(672) + mod_a);
    float w = a + 0.7f * delayed;
    push(&p->ap_a, w);
    a = delayed - 0.7f * w;
    push(&p->del_a1, a);
    a = tap(&p->del_a1, scaled(4453));
    p->damp_a += (1 - p->damping) * (a - p->damp_a);
    a = allpass(&p->ap_a2, scaled(1800), 0.5f, p->damp_a * p->decay);
    push(&p->del_a2, a);
    p->feedback_a = tap(&p->del_a2, scaled(3720));

    float b = x + p->decay * p->feedback_a;
    delayed = tap_frac(&p->ap_b, (float)scaled(908) + mod_b);
    w = b + 0.7f * delayed;
    push(&p->ap_b, w);
    b = delayed - 0.7f * w;
    push(&p->del_b1, b);
    b = tap(&p->del_b1, scaled(4217));
    p->damp_b += (1 - p->damping) * (b - p->damp_b);
    b = allpass(&p->ap_b2, scaled(2656), 0.5f, p->damp_b * p->decay);
    push(&p->del_b2, b);
    p->feedback_b = tap(&p->del_b2, scaled(3163));

    float l = tap(&p->del_b1, scaled(266)) + tap(&p->del_b1, scaled(2974)) - tap(&p->ap_b2, scaled(1913)) +
              tap(&p->del_b2, scaled(1996)) - tap(&p->del_a1, scaled(1990)) - tap(&p->ap_a2, scaled(187)) -
              tap(&p->del_a2, scaled(1066));
    float r = tap(&p->del_a1, scaled(353)) + tap(&p->del_a1, scaled(3627)) - tap(&p->ap_a2, scaled(1228)) +
              tap(&p->del_a2, scaled(2673)) - tap(&p->del_b1, scaled(2111)) - tap(&p->ap_b2, scaled(335)) -
              tap(&p->del_b2, scaled(121));
    *left = l * 0.6f * p->mix;
    *right = r * 0.6f * p->mix;
}

int pingpong_init(PingPong *d, float seconds) {
    memset(d, 0, sizeof *d);
    d->size = (int)(seconds * AUDIO_RATE) + 2;
    d->left = calloc((size_t)d->size, sizeof(float));
    d->right = calloc((size_t)d->size, sizeof(float));
    d->length = d->size - 2;
    d->feedback = 0.4f;
    return d->left && d->right ? 0 : -1;
}

void pingpong_free(PingPong *d) {
    free(d->left);
    free(d->right);
    d->left = d->right = NULL;
}

void pingpong_process(PingPong *d, float in, float *left, float *right) {
    int read = d->pos - d->length;
    if (read < 0) read += d->size;
    float l = d->left[read], r = d->right[read];
    // The repeats darken and thin out a little each time around, like tape.
    d->lp_l += 0.35f * (l - d->lp_l);
    d->lp_r += 0.35f * (r - d->lp_r);
    d->hp_l += 0.012f * (d->lp_l - d->hp_l);
    d->hp_r += 0.012f * (d->lp_r - d->hp_r);
    float fl = d->lp_l - d->hp_l, fr = d->lp_r - d->hp_r;
    d->left[d->pos] = in + fr * d->feedback;
    d->right[d->pos] = fl * d->feedback;
    if (++d->pos == d->size) d->pos = 0;
    *left = l;
    *right = r;
}

void compressor_init(Compressor *c, float threshold_db, float ratio, float attack_ms, float release_ms) {
    c->env = 0;
    c->gain = c->target = 1;
    c->count = 0;
    c->threshold = mexp2(threshold_db / 6.0206f);
    c->ratio = ratio;
    c->attack = 1 - mexp(-1000.0f / (attack_ms * AUDIO_RATE));
    c->release = 1 - mexp(-1000.0f / (release_ms * AUDIO_RATE));
}

float compressor_gain(Compressor *c, float level) {
    c->env += (level > c->env ? c->attack : c->release) * (level - c->env);
    if ((c->count++ & 15) == 0) c->target = c->env > c->threshold ? mpow(c->threshold / c->env, 1 - 1 / c->ratio) : 1;
    c->gain += 0.02f * (c->target - c->gain);
    return c->gain;
}

void limiter_init(Limiter *l, float ceiling) {
    memset(l, 0, sizeof *l);
    l->gain = 1;
    l->ceiling = ceiling;
    l->release = 1 - mexp(-1000.0f / (90.0f * AUDIO_RATE));
}

// The signal waits LIMIT_AHEAD samples, so the gain is already down when a peak comes out.
void limiter_process(Limiter *l, float *left, float *right) {
    float peak = mmax(mabs(*left), mabs(*right));
    if (peak > l->current) l->current = peak;
    float worst = l->current;
    for (int i = 0; i < LIMIT_AHEAD / LIMIT_BLOCK; i++)
        if (l->blocks[i] > worst) worst = l->blocks[i];
    float target = worst > l->ceiling ? l->ceiling / worst : 1;
    if (target < l->gain)
        l->gain += (target - l->gain) * (4.0f / LIMIT_AHEAD);
    else
        l->gain += (target - l->gain) * l->release;
    float out_l = l->left[l->pos] * l->gain, out_r = l->right[l->pos] * l->gain;
    l->left[l->pos] = *left;
    l->right[l->pos] = *right;
    if (++l->pos == LIMIT_AHEAD) l->pos = 0;
    // The block that just filled up: its slot keeps the old block's peak until then, so nothing is missed.
    if (l->pos % LIMIT_BLOCK == 0) {
        int slots = LIMIT_AHEAD / LIMIT_BLOCK;
        l->blocks[(l->pos / LIMIT_BLOCK + slots - 1) % slots] = l->current;
        l->current = 0;
    }
    *left = clampf(out_l, -l->ceiling, l->ceiling);
    *right = clampf(out_r, -l->ceiling, l->ceiling);
}
