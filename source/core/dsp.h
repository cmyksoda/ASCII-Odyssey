// SPDX-License-Identifier: GPL-3.0-only
#ifndef DSP_H
#define DSP_H

// Topology-preserving state variable filter (Zavalishin/Simper): stable under fast cutoff sweeps.
typedef struct {
    float ic1, ic2;
    float a1, a2, a3, k;
} Svf;

void svf_reset(Svf *f);
void svf_set(Svf *f, float hz, float q);
static inline float svf_low(Svf *f, float v0) {
    float v3 = v0 - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2 * v1 - f->ic1;
    f->ic2 = 2 * v2 - f->ic2;
    return v2;
}
static inline float svf_band(Svf *f, float v0) {
    float v3 = v0 - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2 * v1 - f->ic1;
    f->ic2 = 2 * v2 - f->ic2;
    return v1;
}
static inline float svf_high(Svf *f, float v0) {
    float v3 = v0 - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2 * v1 - f->ic1;
    f->ic2 = 2 * v2 - f->ic2;
    return v0 - f->k * v1 - v2;
}
// The 0..1 cutoff knob instruments use, mapped exponentially from 30 Hz to about 19 kHz.
float knob_hz(float knob);

typedef struct {
    float *buf;
    int size, pos;
} DelayLine;

typedef struct {
    float decay, damping, mix;
    float bandwidth_state, damp_a, damp_b, lfo;
    DelayLine pre, in[4], ap_a, del_a1, ap_b, del_b1, ap_a2, del_a2, ap_b2, del_b2;
    float feedback_a, feedback_b;
    float *memory;
} Plate;

int plate_init(Plate *p);
void plate_free(Plate *p);
// Mono in, stereo out: Dattorro's figure-of-eight plate tank.
void plate_process(Plate *p, float in, float *left, float *right);

typedef struct {
    float *left, *right;
    int size, pos, length;
    float feedback, lp_l, lp_r, hp_l, hp_r;
} PingPong;

int pingpong_init(PingPong *d, float seconds);
void pingpong_free(PingPong *d);
void pingpong_process(PingPong *d, float in, float *left, float *right);

typedef struct {
    float env, gain, target;
    float attack, release, threshold, ratio;
    int count;
} Compressor;

void compressor_init(Compressor *c, float threshold_db, float ratio, float attack_ms, float release_ms);
float compressor_gain(Compressor *c, float level);

#define LIMIT_AHEAD 128
#define LIMIT_BLOCK 16

typedef struct {
    float left[LIMIT_AHEAD], right[LIMIT_AHEAD];
    // Peak of each 16-sample block in the window, so the look-ahead maximum costs 8 compares, not 128.
    float blocks[LIMIT_AHEAD / LIMIT_BLOCK], current;
    int pos;
    float gain, ceiling, release;
} Limiter;

void limiter_init(Limiter *l, float ceiling);
void limiter_process(Limiter *l, float *left, float *right);
// tanh's shape for |x| up to 3, at a fraction of its cost.
static inline float soft_clip(float x) {
    if (x > 3) return 1;
    if (x < -3) return -1;
    float x2 = x * x;
    return x * (27 + x2) / (27 + 9 * x2);
}

#endif
