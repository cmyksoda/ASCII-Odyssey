// SPDX-License-Identifier: GPL-3.0-only
#include "synth.h"

#include "dsp.h"
#include "mathx.h"

#include <stdlib.h>
#include <string.h>

#define RATE ((float)AUDIO_RATE)

typedef struct {
    float *s;
    int n;
} Wave;

static Wave wave_new(float seconds) {
    Wave w = {NULL, (int)(seconds * RATE)};
    if (w.n < 64) w.n = 64;
    w.s = calloc((size_t)w.n, sizeof(float));
    if (!w.s) w.n = 0;
    return w;
}

static float note_hz(float note) {
    return 440.0f * mexp2((note - 58.0f) / 12.0f);
}

static float hz_note(float hz) {
    return 58.0f + 12.0f * mlog2(hz / 440.0f);
}

static float white(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

static float polyblep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1;
    }
    if (t > 1 - dt) {
        t = (t - 1) / dt;
        return t * t + t + t + 1;
    }
    return 0;
}

static float saw(float phase, float dt) {
    return 2 * phase - 1 - polyblep(phase, dt);
}

static float pulse(float phase, float dt, float width) {
    float p2 = phase + 1 - width;
    if (p2 >= 1) p2 -= 1;
    return (phase < width ? 1.0f : -1.0f) + polyblep(phase, dt) - polyblep(p2, dt);
}

static float decay_factor(float seconds) {
    return mexp(-1.0f / (seconds * RATE));
}

// Makes the jump from the loop's end back to its start seamless by fading the end into what precedes the start.
static void crossfade_loop(float *d, int start, int end, int fade) {
    if (fade > start) fade = start;
    for (int i = 0; i < fade; i++) {
        float t = (float)i / (float)fade;
        d[end - fade + i] = d[end - fade + i] * (1 - t) + d[start - fade + i] * t;
    }
}

static int keep(Instrument *ins, Wave *w, float root, int loop_start, int loop_length, int top_note) {
    if (ins->sample_count >= MAX_SAMPLES_PER_INSTRUMENT || !w->s) {
        free(w->s);
        return -1;
    }
    Sample *s = &ins->samples[ins->sample_count];
    float peak = 1e-6f;
    for (int i = 0; i < w->n; i++) peak = mmax(peak, mabs(w->s[i]));
    // The wave's own buffer becomes the sample, normalized to just under full scale.
    float scale = 0.92f / peak;
    for (int i = 0; i < w->n; i++) w->s[i] *= scale;
    s->frames = w->s;
    s->length = w->n;
    s->loop_start = loop_start;
    s->loop_length = loop_length;
    s->root_note = root;
    ins->sample_top[ins->sample_count] = (uint8_t)top_note;
    ins->sample_count++;
    w->s = NULL;
    return 0;
}

static int seconds_ticks(float seconds, float bpm) {
    int t = (int)(seconds * bpm / 2.5f + 0.5f);
    return t < 1 ? 1 : t;
}

// Attack up to full, decay to the sustain level, hold, then release to silence.
static void envelope(Envelope *e, float bpm, float attack, float decay, float sustain, float release) {
    int a = seconds_ticks(attack, bpm), d = a + seconds_ticks(decay, bpm), r = d + seconds_ticks(release, bpm);
    *e = (Envelope){.count = 4, .sustain = 2};
    e->tick[0] = 0;
    e->level[0] = attack < 0.004f ? 1.0f : 0.0f;
    e->tick[1] = (uint16_t)a;
    e->level[1] = 1;
    e->tick[2] = (uint16_t)d;
    e->level[2] = sustain;
    e->tick[3] = (uint16_t)r;
    e->level[3] = 0;
}

// One-shots carry their whole shape in the sample; the envelope only stops them when the sample ends.
static void one_shot(Envelope *e) {
    *e = (Envelope){.count = 1, .sustain = -1};
    e->level[0] = 1;
}

static void base(Instrument *ins, const char *name, Role role, float volume) {
    memset(ins, 0, sizeof *ins);
    strncpy(ins->name, name, sizeof ins->name - 1);
    ins->role = role;
    ins->volume = volume;
    ins->fadeout = 0.02f;
    one_shot(&ins->volume_env);
}

/* ---------- Drums ---------- */

static void kick(Instrument *ins, Rng *r, int soft) {
    base(ins, soft ? "soft kick" : "kick", ROLE_KICK, 0.95f);
    float end = rng_between(r, 42, 54), punch = rng_between(r, 90, 150), body = rng_between(r, 0.18f, 0.42f);
    if (soft) body *= 0.8f;
    Wave w = wave_new(body * 2.4f + 0.05f);
    uint32_t seed = rng_u32(r) | 1;
    float phase = 0, amp = 1, k = decay_factor(body), click = 0, lp = 0;
    for (int i = 0; i < w.n; i++) {
        float t = (float)i / RATE;
        float hz = end + punch * mexp(-t / 0.032f) + (soft ? 0 : 70 * mexp(-t / 0.004f));
        phase += hz / RATE;
        if (phase >= 1) phase -= 1;
        float v = msin(phase * TAU_F) * amp;
        if (!soft) {
            lp += 0.3f * (white(&seed) - lp);
            click = lp * mexp(-t / 0.0025f) * 0.6f;
        }
        float attack = t < 0.0015f ? t / 0.0015f : 1;
        w.s[i] = mtanh((v + click) * (soft ? 1.1f : 1.8f)) * attack;
        amp *= k;
    }
    keep(ins, &w, 49, 0, 0, 120);
}

static void kick_chip(Instrument *ins, Rng *r) {
    base(ins, "chip kick", ROLE_KICK, 0.9f);
    float sweep = rng_between(r, 0.04f, 0.08f);
    Wave w = wave_new(0.22f);
    float phase = 0;
    for (int i = 0; i < w.n; i++) {
        float t = (float)i / RATE;
        phase += (45 + 260 * mexp(-t / sweep)) / RATE;
        if (phase >= 1) phase -= 1;
        // A 16-step triangle, like the NES's.
        float tri = phase < 0.5f ? phase * 4 - 1 : 3 - phase * 4;
        tri = (float)mfloor_i(tri * 7.5f) / 7.5f;
        w.s[i] = tri * mexp(-t / 0.09f);
    }
    keep(ins, &w, 49, 0, 0, 120);
}

static void snare(Instrument *ins, Rng *r, int gated, int brush) {
    base(ins, gated ? "gated snare" : brush ? "brush snare" : "snare", ROLE_SNARE, 0.75f);
    float tone_hz = rng_between(r, 165, 205), noise_hz = rng_between(r, 2400, 4200);
    float decay = brush ? 0.16f : rng_between(r, 0.09f, 0.16f);
    float length = gated ? 0.42f : 0.5f;
    Wave w = wave_new(length);
    uint32_t seed = rng_u32(r) | 1;
    Svf band, high;
    svf_reset(&band);
    svf_reset(&high);
    svf_set(&band, noise_hz, 0.7f);
    svf_set(&high, 900, 0.7f);
    float p1 = 0, p2 = 0;
    for (int i = 0; i < w.n; i++) {
        float t = (float)i / RATE;
        p1 += (tone_hz + 60 * mexp(-t / 0.02f)) / RATE;
        p2 += (tone_hz * 1.78f) / RATE;
        if (p1 >= 1) p1 -= 1;
        if (p2 >= 1) p2 -= 1;
        float tone = (msin(p1 * TAU_F) + 0.4f * msin(p2 * TAU_F)) * mexp(-t / 0.045f) * (brush ? 0.15f : 0.7f);
        float n = white(&seed);
        float hiss = svf_high(&high, svf_band(&band, n) * 1.6f + n * 0.25f);
        float env = mexp(-t / decay);
        if (gated) env = t < 0.26f ? 0.55f + 0.45f * mexp(-t / 0.05f) : 0.55f * mexp(-(t - 0.26f) / 0.012f);
        if (brush) env *= t < 0.012f ? t / 0.012f : 1;
        w.s[i] = (tone + hiss * env) * (t < 0.001f ? t / 0.001f : 1);
    }
    keep(ins, &w, 49, 0, 0, 120);
    if (gated) ins->send_reverb = 0.3f;
}

static void snare_chip(Instrument *ins, Rng *r) {
    base(ins, "chip snare", ROLE_SNARE, 0.7f);
    Wave w = wave_new(0.2f);
    uint32_t lfsr = 0x4000 | (rng_u32(r) & 0x3fff);
    float hold = 0, out = 0;
    for (int i = 0; i < w.n; i++) {
        float t = (float)i / RATE;
        // A 15-bit LFSR clocked like an 8-bit sound chip's noise channel.
        hold += 9000 / RATE;
        if (hold >= 1) {
            hold -= 1;
            uint32_t bit = (lfsr ^ (lfsr >> 1)) & 1;
            lfsr = (lfsr >> 1) | (bit << 14);
            out = lfsr & 1 ? 1.0f : -1.0f;
        }
        w.s[i] = out * mexp(-t / 0.07f);
    }
    keep(ins, &w, 49, 0, 0, 120);
}

static void clap(Instrument *ins, Rng *r) {
    base(ins, "clap", ROLE_SNARE, 0.7f);
    Wave w = wave_new(0.45f);
    uint32_t seed = rng_u32(r) | 1;
    Svf band;
    svf_reset(&band);
    svf_set(&band, rng_between(r, 1000, 1500), 1.1f);
    float spread = rng_between(r, 0.008f, 0.013f);
    for (int i = 0; i < w.n; i++) {
        float t = (float)i / RATE, env = 0;
        for (int b = 0; b < 3; b++) {
            float tb = t - spread * (float)b;
            if (tb >= 0) env += mexp(-tb / 0.004f);
        }
        float tail = t - spread * 3;
        if (tail >= 0) env += 0.85f * mexp(-tail / 0.13f);
        w.s[i] = svf_band(&band, white(&seed)) * env;
    }
    keep(ins, &w, 49, 0, 0, 120);
    ins->send_reverb = 0.25f;
}

static const float HAT_RATIOS[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};

static void metal(Wave *w, uint32_t *seed, float scale, float noise, float decay, float hp_hz, float band_hz) {
    float phase[6] = {0};
    Svf band, high;
    svf_reset(&band);
    svf_reset(&high);
    svf_set(&band, band_hz, 0.8f);
    svf_set(&high, hp_hz, 0.7f);
    for (int i = 0; i < w->n; i++) {
        float t = (float)i / RATE, v = 0;
        for (int k = 0; k < 6; k++) {
            phase[k] += HAT_RATIOS[k] * scale / RATE;
            if (phase[k] >= 1) phase[k] -= 1;
            v += phase[k] < 0.5f ? 1.0f : -1.0f;
        }
        v = v / 6 * 0.7f + white(seed) * noise;
        w->s[i] = svf_high(&high, svf_band(&band, v)) * mexp(-t / decay) * (t < 0.0008f ? t / 0.0008f : 1);
    }
}

static void hat(Instrument *ins, Rng *r, int open) {
    base(ins, open ? "open hat" : "closed hat", ROLE_HAT, open ? 0.32f : 0.4f);
    uint32_t seed = rng_u32(r) | 1;
    float decay = open ? rng_between(r, 0.18f, 0.35f) : rng_between(r, 0.025f, 0.05f);
    Wave w = wave_new(decay * 5);
    metal(&w, &seed, rng_between(r, 1.5f, 2.0f), 0.45f, decay, open ? 6500 : 7000, 9500);
    keep(ins, &w, 49, 0, 0, 120);
    ins->pan = open ? -0.2f : 0.25f;
    ins->send_reverb = 0.05f;
}

static void hat_chip(Instrument *ins, Rng *r) {
    base(ins, "chip hat", ROLE_HAT, 0.35f);
    Wave w = wave_new(0.08f);
    // The short-period noise mode: a 93-step loop that rings metallic.
    uint32_t lfsr = 1 + (rng_u32(r) & 0x7f);
    float hold = 0, out = 0;
    for (int i = 0; i < w.n; i++) {
        float t = (float)i / RATE;
        hold += 22000 / RATE;
        if (hold >= 1) {
            hold -= 1;
            uint32_t bit = (lfsr ^ (lfsr >> 6)) & 1;
            lfsr = ((lfsr >> 1) | (bit << 14)) & 0x7fff;
            out = lfsr & 1 ? 1.0f : -1.0f;
        }
        w.s[i] = out * mexp(-t / 0.018f);
    }
    keep(ins, &w, 49, 0, 0, 120);
}

static void shaker(Instrument *ins, Rng *r) {
    base(ins, "shaker", ROLE_HAT, 0.3f);
    Wave w = wave_new(0.18f);
    uint32_t seed = rng_u32(r) | 1;
    Svf band;
    svf_reset(&band);
    svf_set(&band, rng_between(r, 5500, 8000), 1.2f);
    for (int i = 0; i < w.n; i++) {
        float t = (float)i / RATE;
        float env = t < 0.012f ? t / 0.012f : mexp(-(t - 0.012f) / 0.05f);
        w.s[i] = svf_band(&band, white(&seed)) * env;
    }
    keep(ins, &w, 49, 0, 0, 120);
    ins->pan = rng_between(r, -0.4f, 0.4f);
}

static void rim(Instrument *ins, Rng *r) {
    base(ins, "rim", ROLE_PERC, 0.35f);
    Wave w = wave_new(0.1f);
    float hz = rng_between(r, 1500, 1900);
    for (int i = 0; i < w.n; i++) {
        float t = (float)i / RATE;
        w.s[i] = (0.6f * msin(TAU_F * hz * t) + 0.4f * msin(TAU_F * hz * 0.49f * t)) * mexp(-t / 0.012f);
    }
    keep(ins, &w, 49, 0, 0, 120);
    ins->send_reverb = 0.15f;
    ins->send_delay = 0.15f;
}

static void tom(Instrument *ins, Rng *r) {
    base(ins, "tom", ROLE_PERC, 0.6f);
    Wave w = wave_new(0.6f);
    uint32_t seed = rng_u32(r) | 1;
    float hz = rng_between(r, 95, 140), phase = 0;
    for (int i = 0; i < w.n; i++) {
        float t = (float)i / RATE;
        phase += (hz * (1 + 0.6f * mexp(-t / 0.05f))) / RATE;
        if (phase >= 1) phase -= 1;
        w.s[i] = (msin(phase * TAU_F) + white(&seed) * 0.12f * mexp(-t / 0.01f)) * mexp(-t / 0.22f);
    }
    keep(ins, &w, 49, 0, 0, 120);
    ins->send_reverb = 0.2f;
}

static void cymbal(Instrument *ins, Rng *r, int ride) {
    base(ins, ride ? "ride" : "crash", ride ? ROLE_HAT : ROLE_CRASH, ride ? 0.3f : 0.42f);
    uint32_t seed = rng_u32(r) | 1;
    float decay = ride ? rng_between(r, 0.6f, 1.0f) : rng_between(r, 1.2f, 2.2f);
    Wave w = wave_new(decay * 3.5f);
    metal(&w, &seed, rng_between(r, 1.1f, 1.6f), ride ? 0.35f : 0.8f, decay, 3800, ride ? 7000 : 9000);
    if (ride)
        for (int i = 0; i < w.n; i++) {
            float t = (float)i / RATE;
            w.s[i] += 0.25f * msin(TAU_F * 3100 * t) * mexp(-t / 0.4f);
        }
    keep(ins, &w, 49, 0, 0, 120);
    ins->send_reverb = 0.2f;
}

// Jungle breaks: drier, grittier hits that sound sampled off a record.
static void break_hit(Instrument *ins, Rng *r, int snare_hit) {
    if (snare_hit)
        snare(ins, r, 0, 0);
    else
        kick(ins, r, 0);
    strncpy(ins->name, snare_hit ? "break snare" : "break kick", sizeof ins->name - 1);
    Sample *s = &ins->samples[0];
    uint32_t seed = rng_u32(r) | 1;
    for (int i = 0; i < s->length; i++) {
        float v = mtanh(s->frames[i] * 2.2f) * 0.8f + white(&seed) * 0.015f;
        // Twelve-bit grit, like an old sampler.
        s->frames[i] = (float)((int)(v * 2048.0f)) / 2048.0f;
    }
    ins->volume = snare_hit ? 0.7f : 0.85f;
}

/* ---------- Bass ---------- */

// A static waveform loops perfectly over whole cycles; 256-frame cycles sit at 187.5 Hz.
#define CYCLE 256
#define CYCLE_HZ (RATE / CYCLE)

static void single_cycle(Instrument *ins, float (*shape)(float phase, void *ctx), void *ctx, int cycles) {
    Wave w = wave_new((float)(CYCLE * cycles) / RATE);
    float dt = 1.0f / CYCLE;
    for (int i = 0; i < w.n; i++) {
        float phase = (float)(i % CYCLE) * dt;
        w.s[i] = shape(phase, ctx);
    }
    keep(ins, &w, hz_note(CYCLE_HZ), 0, w.n, 120);
}

static float sub_shape(float phase, void *ctx) {
    (void)ctx;
    return msin(phase * TAU_F) + 0.12f * msin(phase * TAU_F * 2);
}

static float chip_triangle(float phase, void *ctx) {
    (void)ctx;
    float tri = phase < 0.5f ? phase * 4 - 1 : 3 - phase * 4;
    return (float)mfloor_i(tri * 7.5f + 0.5f) / 7.5f;
}

static float chip_pulse(float phase, void *ctx) {
    float width = *(float *)ctx;
    return phase < width ? 1.0f : -1.0f;
}

static void bass_sub(Instrument *ins, Rng *r, float bpm) {
    (void)r;
    base(ins, "sub bass", ROLE_BASS, 0.8f);
    single_cycle(ins, sub_shape, NULL, 1);
    envelope(&ins->volume_env, bpm, 0.008f, 0.1f, 0.9f, 0.12f);
    ins->ducked = 1;
}

static void bass_chip(Instrument *ins, Rng *r, float bpm) {
    (void)r;
    base(ins, "chip bass", ROLE_BASS, 0.75f);
    single_cycle(ins, chip_triangle, NULL, 1);
    envelope(&ins->volume_env, bpm, 0.002f, 0.05f, 1.0f, 0.03f);
}

// Saw-family sounds get one sample per two octaves, so high notes never alias badly.
static void saw_stack(Instrument *ins, Rng *r, int voices, float spread_cents, float seconds, float lowpass_hz,
                      float root_lo, float pulse_mix) {
    float roots[3] = {root_lo, root_lo + 24, root_lo + 48};
    float phases[9];
    for (int v = 0; v < voices; v++) phases[v] = rng_float(r);
    for (int k = 0; k < 3; k++) {
        Wave w = wave_new(seconds);
        float hz = note_hz(roots[k]);
        Svf lp;
        svf_reset(&lp);
        svf_set(&lp, mmin(lowpass_hz * mexp2((float)k), RATE * 0.45f), 0.7f);
        float ph[9], dts[9];
        memcpy(ph, phases, sizeof ph);
        for (int s = 0; s < voices; s++) {
            float cents = voices > 1 ? spread_cents * ((float)s / (float)(voices - 1) * 2 - 1) : 0;
            dts[s] = hz * mexp2(cents / 1200.0f) / RATE;
        }
        for (int i = 0; i < w.n; i++) {
            float v = 0;
            for (int s = 0; s < voices; s++) {
                float dt = dts[s];
                ph[s] += dt;
                if (ph[s] >= 1) ph[s] -= 1;
                float o = saw(ph[s], dt);
                if (pulse_mix > 0 && s == 0) o = o * (1 - pulse_mix) + pulse(ph[s], dt, 0.5f) * pulse_mix;
                v += o * (s == voices / 2 ? 1.1f : 1.0f);
            }
            w.s[i] = svf_low(&lp, v / msqrt((float)voices));
        }
        int loop_start = w.n / 3, fade = w.n / 6;
        crossfade_loop(w.s, loop_start, w.n, fade);
        keep(ins, &w, roots[k], loop_start, w.n - loop_start, k == 2 ? 120 : (int)roots[k] + 18);
    }
}

static void bass_saw(Instrument *ins, Rng *r, float bpm) {
    base(ins, "saw bass", ROLE_BASS, 0.6f);
    saw_stack(ins, r, 1, 0, 0.6f, 9000, 25, 0);
    envelope(&ins->volume_env, bpm, 0.003f, rng_between(r, 0.12f, 0.3f), 0.55f, 0.08f);
    ins->cutoff = rng_between(r, 0.42f, 0.56f);
    ins->resonance = rng_between(r, 0.8f, 1.6f);
    ins->cutoff_env = rng_between(r, 0.18f, 0.3f);
    ins->cutoff_decay = rng_between(r, 0.08f, 0.2f);
    ins->ducked = 1;
}

static void bass_reese(Instrument *ins, Rng *r, float bpm) {
    base(ins, "reese bass", ROLE_BASS, 0.55f);
    saw_stack(ins, r, 3, rng_between(r, 14, 24), 2.4f, 2500, 25, 0);
    envelope(&ins->volume_env, bpm, 0.01f, 0.2f, 0.9f, 0.15f);
    ins->cutoff = 0.62f;
    ins->resonance = 0.9f;
    ins->ducked = 1;
}

static void fm_tone(Wave *w, float hz, float ratio, float index, float index_decay, float amp_decay, float attack,
                    float feedback) {
    float pc = 0, pm = 0, prev = 0;
    float ki = decay_factor(index_decay), ka = decay_factor(amp_decay), idx = index, amp = 1;
    for (int i = 0; i < w->n; i++) {
        float t = (float)i / RATE;
        pm += hz * ratio / RATE;
        pc += hz / RATE;
        if (pm >= 1) pm -= 1;
        if (pc >= 1) pc -= 1;
        float mod = msin(pm * TAU_F + prev * feedback);
        prev = mod;
        float v = msin(pc * TAU_F + idx * mod);
        w->s[i] = v * amp * (attack > 0 && t < attack ? t / attack : 1);
        idx *= ki;
        amp *= ka;
    }
}

static void bass_fm(Instrument *ins, Rng *r, float bpm) {
    base(ins, "fm bass", ROLE_BASS, 0.65f);
    float ratio = rng_chance(r, 50) ? 1.0f : 2.0f, index = rng_between(r, 1.5f, 3.2f);
    for (int k = 0; k < 2; k++) {
        float root = 25.0f + 24.0f * (float)k;
        Wave w = wave_new(1.2f);
        fm_tone(&w, note_hz(root), ratio, index, rng_between(r, 0.12f, 0.3f), 30, 0.002f, 0.2f);
        int loop_start = w.n * 2 / 3;
        crossfade_loop(w.s, loop_start, w.n, w.n / 8);
        keep(ins, &w, root, loop_start, w.n - loop_start, k ? 120 : 43);
    }
    envelope(&ins->volume_env, bpm, 0.003f, 0.25f, 0.6f, 0.1f);
    ins->ducked = 1;
}

/* ---------- Pads and keys ---------- */

static void pad_supersaw(Instrument *ins, Rng *r, float bpm) {
    base(ins, "supersaw pad", ROLE_PAD, 0.32f);
    saw_stack(ins, r, 7, rng_between(r, 14, 30), 3.0f, 12000, 37, 0);
    envelope(&ins->volume_env, bpm, rng_between(r, 0.3f, 0.9f), 0.5f, 0.85f, rng_between(r, 0.8f, 1.6f));
    ins->cutoff = rng_between(r, 0.62f, 0.78f);
    ins->resonance = 0.8f;
    ins->send_reverb = 0.38f;
    ins->ducked = 1;
    ins->fadeout = 0.01f;
}

static void pad_warm(Instrument *ins, Rng *r, float bpm) {
    base(ins, "warm pad", ROLE_PAD, 0.36f);
    saw_stack(ins, r, 3, rng_between(r, 6, 12), 3.0f, 2400, 37, 0.3f);
    envelope(&ins->volume_env, bpm, rng_between(r, 0.6f, 1.4f), 0.5f, 0.9f, rng_between(r, 1.0f, 2.0f));
    ins->cutoff = 0.7f;
    ins->send_reverb = 0.45f;
    ins->ducked = 1;
    ins->fadeout = 0.008f;
}

// A buzzy source through three vowel formants, with a slow, human vibrato.
static void pad_choir(Instrument *ins, Rng *r, float bpm) {
    base(ins, "choir", ROLE_PAD, 0.4f);
    static const float vowels[3][3] = {{730, 1090, 2440}, {300, 870, 2240}, {570, 840, 2410}};
    const float *f = vowels[rng_range(r, 0, 3)];
    for (int k = 0; k < 2; k++) {
        float root = 37.0f + 24.0f * (float)k;
        Wave w = wave_new(3.0f);
        float hz = note_hz(root), phase = 0, vib = 0;
        Svf bands[3];
        for (int b = 0; b < 3; b++) {
            svf_reset(&bands[b]);
            svf_set(&bands[b], f[b], 6.0f);
        }
        for (int i = 0; i < w.n; i++) {
            vib += 5.2f / RATE;
            float dt = hz * (1 + 0.006f * msin(vib * TAU_F)) / RATE;
            phase += dt;
            if (phase >= 1) phase -= 1;
            float src = saw(phase, dt);
            w.s[i] = svf_band(&bands[0], src) + 0.6f * svf_band(&bands[1], src) + 0.25f * svf_band(&bands[2], src);
        }
        int loop_start = w.n / 3;
        crossfade_loop(w.s, loop_start, w.n, w.n / 5);
        keep(ins, &w, root, loop_start, w.n - loop_start, k ? 120 : 55);
    }
    envelope(&ins->volume_env, bpm, rng_between(r, 0.5f, 1.2f), 0.4f, 0.9f, 1.4f);
    ins->send_reverb = 0.5f;
    ins->ducked = 1;
}

static void pad_glass(Instrument *ins, Rng *r, float bpm) {
    base(ins, "glass pad", ROLE_PAD, 0.32f);
    float ratio = rng_chance(r, 50) ? 2.0f : 3.0f;
    for (int k = 0; k < 2; k++) {
        float root = 37.0f + 24.0f * (float)k;
        Wave w = wave_new(3.0f);
        Wave w2 = wave_new(3.0f);
        fm_tone(&w, note_hz(root), ratio, 1.2f, 4.0f, 1000, 0, 0);
        fm_tone(&w2, note_hz(root) * 1.004f, ratio, 1.0f, 4.0f, 1000, 0, 0);
        for (int i = 0; i < w.n && w2.s; i++) w.s[i] = (w.s[i] + w2.s[i]) * 0.5f;
        free(w2.s);
        int loop_start = w.n / 3;
        crossfade_loop(w.s, loop_start, w.n, w.n / 5);
        keep(ins, &w, root, loop_start, w.n - loop_start, k ? 120 : 55);
    }
    envelope(&ins->volume_env, bpm, rng_between(r, 0.4f, 1.0f), 0.5f, 0.85f, 1.6f);
    ins->send_reverb = 0.55f;
    ins->send_delay = 0.15f;
    ins->ducked = 1;
}

// A Rhodes-like electric piano: a soft 1:1 FM body and a quick 14:1 tine.
static void keys_epiano(Instrument *ins, Rng *r, float bpm) {
    (void)bpm;
    base(ins, "electric piano", ROLE_CHORD, 0.45f);
    float bell = rng_between(r, 0.15f, 0.35f);
    for (int k = 0; k < 3; k++) {
        float root = 37.0f + 18.0f * (float)k;
        Wave w = wave_new(2.6f), tine = wave_new(2.6f);
        fm_tone(&w, note_hz(root), 1.0f, 1.6f, 0.6f, 1.1f, 0.002f, 0);
        fm_tone(&tine, note_hz(root), 14.0f, 0.8f, 0.04f, 0.25f, 0.001f, 0);
        for (int i = 0; i < w.n && tine.s; i++) w.s[i] += tine.s[i] * bell;
        free(tine.s);
        keep(ins, &w, root, 0, 0, k == 2 ? 120 : (int)root + 9);
    }
    ins->send_reverb = 0.25f;
    ins->fadeout = 0.05f;
}

static float organ_shape(float phase, void *ctx) {
    const float *bars = ctx;
    return bars[0] * msin(phase * TAU_F) + bars[1] * msin(phase * TAU_F * 2) + bars[2] * msin(phase * TAU_F * 3) +
           bars[3] * msin(phase * TAU_F * 4) + bars[4] * msin(phase * TAU_F * 6);
}

static void keys_organ(Instrument *ins, Rng *r, float bpm) {
    base(ins, "organ", ROLE_CHORD, 0.32f);
    float bars[5] = {1, rng_between(r, 0.3f, 0.8f), rng_between(r, 0.1f, 0.5f), rng_between(r, 0, 0.4f),
                     rng_between(r, 0, 0.2f)};
    single_cycle(ins, organ_shape, bars, 1);
    envelope(&ins->volume_env, bpm, 0.01f, 0.1f, 1.0f, 0.15f);
    ins->send_reverb = 0.3f;
    ins->vibrato_depth = 0.08f;
    ins->vibrato_rate = 6.0f;
}

/* ---------- Leads, arps and plucks ---------- */

static void lead_saw(Instrument *ins, Rng *r, float bpm) {
    base(ins, "saw lead", ROLE_LEAD, 0.42f);
    saw_stack(ins, r, 2, rng_between(r, 6, 12), 1.2f, 7000, 49, 0.35f);
    envelope(&ins->volume_env, bpm, 0.006f, 0.25f, 0.8f, 0.15f);
    ins->cutoff = rng_between(r, 0.7f, 0.85f);
    ins->cutoff_env = 0.12f;
    ins->cutoff_decay = 0.2f;
    ins->send_reverb = 0.22f;
    ins->send_delay = 0.25f;
    ins->vibrato_depth = rng_between(r, 0.08f, 0.16f);
    ins->vibrato_rate = rng_between(r, 5.0f, 6.2f);
}

static void lead_square(Instrument *ins, Rng *r, float bpm) {
    base(ins, "square lead", ROLE_LEAD, 0.38f);
    for (int k = 0; k < 3; k++) {
        float root = 37.0f + 24.0f * (float)k;
        Wave w = wave_new(1.5f);
        float hz = note_hz(root), phase = 0, lfo = rng_float(r);
        for (int i = 0; i < w.n; i++) {
            float dt = hz / RATE;
            phase += dt;
            if (phase >= 1) phase -= 1;
            // Pulse width drifting between 30 and 50 percent, the classic PWM shimmer.
            lfo += 0.7f / RATE;
            w.s[i] = pulse(phase, dt, 0.4f + 0.1f * msin(lfo * TAU_F));
        }
        int loop_start = w.n / 3;
        crossfade_loop(w.s, loop_start, w.n, w.n / 6);
        keep(ins, &w, root, loop_start, w.n - loop_start, k == 2 ? 120 : (int)root + 18);
    }
    envelope(&ins->volume_env, bpm, 0.005f, 0.2f, 0.85f, 0.15f);
    ins->cutoff = 0.72f;
    ins->send_reverb = 0.2f;
    ins->send_delay = 0.3f;
    ins->vibrato_depth = 0.1f;
    ins->vibrato_rate = 5.5f;
}

static float flute_shape(float phase, void *ctx) {
    (void)ctx;
    float tri = phase < 0.5f ? phase * 4 - 1 : 3 - phase * 4;
    return 0.8f * msin(phase * TAU_F) + 0.2f * tri;
}

static void lead_sine(Instrument *ins, Rng *r, float bpm) {
    base(ins, "soft lead", ROLE_LEAD, 0.45f);
    single_cycle(ins, flute_shape, NULL, 1);
    envelope(&ins->volume_env, bpm, rng_between(r, 0.02f, 0.08f), 0.3f, 0.85f, 0.3f);
    ins->send_reverb = 0.35f;
    ins->send_delay = 0.3f;
    ins->vibrato_depth = rng_between(r, 0.1f, 0.2f);
    ins->vibrato_rate = rng_between(r, 4.5f, 5.8f);
}

static void lead_chip(Instrument *ins, Rng *r, float bpm) {
    base(ins, "chip lead", ROLE_LEAD, 0.34f);
    static float widths[3] = {0.125f, 0.25f, 0.5f};
    float width = widths[rng_range(r, 0, 3)];
    single_cycle(ins, chip_pulse, &width, 1);
    envelope(&ins->volume_env, bpm, 0.002f, rng_between(r, 0.1f, 0.4f), rng_between(r, 0.5f, 0.8f), 0.05f);
    ins->send_delay = 0.2f;
    ins->vibrato_depth = rng_between(r, 0.1f, 0.25f);
    ins->vibrato_rate = 6.5f;
}

static void arp_pluck(Instrument *ins, Rng *r, float bpm) {
    (void)bpm;
    base(ins, "pluck synth", ROLE_ARP, 0.4f);
    float decay = rng_between(r, 0.12f, 0.3f), bright = rng_between(r, 3000, 7000);
    for (int k = 0; k < 3; k++) {
        float root = 37.0f + 24.0f * (float)k;
        Wave w = wave_new(0.8f);
        float hz = note_hz(root), ph1 = 0, ph2 = rng_float(r);
        Svf lp;
        svf_reset(&lp);
        for (int i = 0; i < w.n; i++) {
            float t = (float)i / RATE, dt = hz / RATE;
            if ((i & 31) == 0) svf_set(&lp, 200 + bright * mexp(-t / (decay * 0.5f)), 1.1f);
            ph1 += dt;
            ph2 += dt * 1.004f;
            if (ph1 >= 1) ph1 -= 1;
            if (ph2 >= 1) ph2 -= 1;
            w.s[i] = svf_low(&lp, saw(ph1, dt) + 0.6f * saw(ph2, dt * 1.004f)) * mexp(-t / decay);
        }
        keep(ins, &w, root, 0, 0, k == 2 ? 120 : (int)root + 18);
    }
    ins->send_reverb = 0.15f;
    ins->send_delay = 0.25f;
    ins->ducked = 1;
}

static void arp_chip(Instrument *ins, Rng *r, float bpm) {
    base(ins, "chip arp", ROLE_ARP, 0.3f);
    float width = rng_chance(r, 50) ? 0.125f : 0.25f;
    single_cycle(ins, chip_pulse, &width, 1);
    envelope(&ins->volume_env, bpm, 0.002f, rng_between(r, 0.06f, 0.18f), 0.35f, 0.04f);
    ins->send_delay = 0.15f;
}

// Karplus-Strong with an allpass fractional delay, so every note is in tune; one sample per octave.
static void karplus(Instrument *ins, Rng *r, float sustain, float brightness, float pick, int harp) {
    uint32_t seed = rng_u32(r) | 1;
    for (int k = 0; k < 5; k++) {
        float root = 25.0f + 12.0f * (float)k;
        float hz = note_hz(root);
        Wave w = wave_new(mmin(sustain * 1.4f, 4.5f));
        float period = RATE / hz - 0.5f;
        int length = mfloor_i(period);
        float frac = period - (float)length;
        if (frac < 0.2f) {
            length -= 1;
            frac += 1;
        }
        float c = (1 - frac) / (1 + frac);
        float *line = calloc((size_t)length + 2, sizeof(float));
        if (!line) {
            free(w.s);
            continue;
        }
        float lp = 0;
        for (int i = 0; i < length; i++) {
            lp += brightness * (white(&seed) - lp);
            line[i] = lp;
        }
        int pick_at = (int)((float)length * pick);
        for (int i = length - 1; i >= pick_at && pick_at > 0; i--) line[i] -= 0.6f * line[i - pick_at];
        float rho = mpow(10.0f, -3.0f / (hz * sustain));
        float prev = 0, x1 = 0, y1 = 0, dc_x = 0, dc_y = 0;
        int p = 0;
        for (int i = 0; i < w.n; i++) {
            float out = line[p];
            float avg = 0.5f * (out + prev) * rho;
            prev = out;
            float y = c * avg + x1 - c * y1;
            x1 = avg;
            y1 = y;
            line[p] = y;
            if (++p == length) p = 0;
            dc_y = out - dc_x + 0.996f * dc_y;
            dc_x = out;
            w.s[i] = dc_y * (harp && i < 96 ? (float)i / 96 : 1);
        }
        free(line);
        keep(ins, &w, root, 0, 0, k == 4 ? 120 : (int)root + 6);
    }
}

static void pluck_ks(Instrument *ins, Rng *r, float bpm) {
    (void)bpm;
    base(ins, "plucked string", ROLE_PLUCK, 0.55f);
    karplus(ins, r, rng_between(r, 1.8f, 3.5f), rng_between(r, 0.4f, 0.8f), rng_between(r, 0.1f, 0.2f), 0);
    ins->send_reverb = 0.32f;
    ins->send_delay = 0.2f;
    ins->fadeout = 0.04f;
}

static void harp(Instrument *ins, Rng *r, float bpm) {
    (void)bpm;
    base(ins, "harp", ROLE_PLUCK, 0.5f);
    karplus(ins, r, rng_between(r, 3.0f, 4.5f), rng_between(r, 0.25f, 0.45f), 0.5f, 1);
    ins->send_reverb = 0.45f;
    ins->fadeout = 0.03f;
}

static void bell_fm(Instrument *ins, Rng *r, float bpm) {
    (void)bpm;
    base(ins, "bell", ROLE_BELL, 0.34f);
    static const float ratios[4] = {3.5f, 2.76f, 1.4f, 4.0f};
    float ratio = ratios[rng_range(r, 0, 4)], decay = rng_between(r, 1.6f, 3.2f);
    for (int k = 0; k < 3; k++) {
        float root = 49.0f + 18.0f * (float)k;
        Wave w = wave_new(decay * 1.5f), shimmer = wave_new(decay * 1.5f);
        fm_tone(&w, note_hz(root), ratio, 3.0f, 0.12f, decay, 0.0015f, 0);
        fm_tone(&shimmer, note_hz(root) * 2.001f, 1.001f, 0.15f, 0.2f, decay * 0.4f, 0.0015f, 0);
        for (int i = 0; i < w.n && shimmer.s; i++) w.s[i] = 0.8f * w.s[i] + 0.3f * shimmer.s[i];
        free(shimmer.s);
        keep(ins, &w, root, 0, 0, k == 2 ? 120 : (int)root + 9);
    }
    ins->send_reverb = 0.45f;
    ins->send_delay = 0.28f;
    ins->fadeout = 0.02f;
}

static void celesta(Instrument *ins, Rng *r, float bpm, int musicbox) {
    (void)bpm;
    base(ins, musicbox ? "music box" : "celesta", ROLE_BELL, 0.38f);
    float ratio = musicbox ? rng_between(r, 5.2f, 5.6f) : 4.0f, decay = musicbox ? 2.2f : 1.4f;
    for (int k = 0; k < 3; k++) {
        float root = 49.0f + 18.0f * (float)k;
        Wave w = wave_new(decay * 1.6f), tine = wave_new(decay * 1.6f);
        fm_tone(&w, note_hz(root), 1.0f, 0.3f, 0.3f, decay, 0.001f, 0);
        fm_tone(&tine, note_hz(root) * ratio, 1.0f, 0.2f, 0.1f, decay * 0.15f, 0.0005f, 0);
        for (int i = 0; i < w.n && tine.s; i++) w.s[i] += tine.s[i] * 0.35f;
        free(tine.s);
        keep(ins, &w, root, 0, 0, k == 2 ? 120 : (int)root + 9);
    }
    ins->send_reverb = 0.42f;
    ins->send_delay = 0.2f;
}

/* ---------- FX ---------- */

static void riser(Instrument *ins, Rng *r, float bpm, int down) {
    base(ins, down ? "downlifter" : "riser", ROLE_FX, 0.45f);
    float seconds = (down ? 4.0f : 8.0f) * 60.0f / bpm;
    Wave w = wave_new(seconds);
    uint32_t seed = rng_u32(r) | 1;
    Svf band;
    svf_reset(&band);
    float phase = 0;
    for (int i = 0; i < w.n; i++) {
        float x = (float)i / (float)w.n, u = down ? 1 - x : x;
        if ((i & 31) == 0) svf_set(&band, 200 * mpow(40.0f, u * 0.75f + u * u * 0.25f), 2.0f);
        phase += 55 * mpow(4.0f, u) / RATE;
        if (phase >= 1) phase -= 1;
        float level = down ? (1 - x) * (1 - x) : x * x;
        w.s[i] = (svf_band(&band, white(&seed)) + 0.08f * saw(phase, 0.001f) * u) * level;
    }
    keep(ins, &w, 49, 0, 0, 120);
    ins->send_reverb = 0.35f;
}

static void impact(Instrument *ins, Rng *r) {
    base(ins, "impact", ROLE_FX, 0.6f);
    Wave w = wave_new(2.6f);
    uint32_t seed = rng_u32(r) | 1;
    Svf lp;
    svf_reset(&lp);
    float phase = 0;
    for (int i = 0; i < w.n; i++) {
        float t = (float)i / RATE;
        if ((i & 31) == 0) svf_set(&lp, 90 + 2400 * mexp(-t / 0.12f), 0.7f);
        phase += (28 + 58 * mexp(-t / 0.35f)) / RATE;
        if (phase >= 1) phase -= 1;
        float boom = msin(phase * TAU_F) * mexp(-t / 0.9f);
        w.s[i] = mtanh(1.3f * (boom + svf_low(&lp, white(&seed)) * mexp(-t / 0.35f)));
    }
    keep(ins, &w, 49, 0, 0, 120);
    ins->send_reverb = 0.35f;
}

static void reverse_crash(Instrument *ins, Rng *r, float bpm) {
    cymbal(ins, r, 0);
    strncpy(ins->name, "reverse crash", sizeof ins->name - 1);
    ins->role = ROLE_FX;
    Sample *s = &ins->samples[0];
    int keep_frames = (int)(4 * 60.0f / bpm * RATE);
    if (keep_frames > s->length) keep_frames = s->length;
    float *rev = malloc(sizeof(float) * (size_t)keep_frames);
    if (!rev) return;
    for (int i = 0; i < keep_frames; i++) {
        float x = (float)i / (float)keep_frames;
        rev[i] = s->frames[keep_frames - 1 - i] * x * x;
    }
    free(s->frames);
    s->frames = rev;
    s->length = keep_frames;
    ins->volume = 0.35f;
}

static void crackle(Instrument *ins, Rng *r) {
    base(ins, "vinyl", ROLE_FX, 0.18f);
    Wave w = wave_new(4.0f);
    uint32_t seed = rng_u32(r) | 1;
    Svf hiss;
    svf_reset(&hiss);
    svf_set(&hiss, 3500, 0.6f);
    for (int i = 0; i < w.n; i++) {
        float n = white(&seed);
        float pop = (white(&seed) > 0.9993f) ? white(&seed) * 3 : 0;
        w.s[i] = svf_low(&hiss, n) * 0.15f + pop;
    }
    int loop_start = w.n / 4;
    crossfade_loop(w.s, loop_start, w.n, w.n / 8);
    keep(ins, &w, 49, loop_start, w.n - loop_start, 120);
    envelope(&ins->volume_env, 120, 0.5f, 0.1f, 1.0f, 1.0f);
}

static void wind(Instrument *ins, Rng *r) {
    base(ins, "wind", ROLE_FX, 0.3f);
    Wave w = wave_new(6.0f);
    uint32_t seed = rng_u32(r) | 1;
    Svf band;
    svf_reset(&band);
    for (int i = 0; i < w.n; i++) {
        float t = (float)i / RATE;
        if ((i & 63) == 0) svf_set(&band, 500 + 350 * msin(t * 1.1f) + 150 * msin(t * 2.9f + 1), 1.6f);
        w.s[i] = svf_band(&band, white(&seed));
    }
    int loop_start = w.n / 3;
    crossfade_loop(w.s, loop_start, w.n, w.n / 4);
    keep(ins, &w, 49, loop_start, w.n - loop_start, 120);
    envelope(&ins->volume_env, 120, 1.5f, 0.1f, 1.0f, 2.0f);
    ins->send_reverb = 0.4f;
}

int synth_instrument(Instrument *ins, VoiceKind kind, Rng *r, float bpm) {
    switch (kind) {
    case VOICE_KICK: kick(ins, r, 0); break;
    case VOICE_KICK_SOFT: kick(ins, r, 1); break;
    case VOICE_KICK_CHIP: kick_chip(ins, r); break;
    case VOICE_SNARE: snare(ins, r, 0, 0); break;
    case VOICE_SNARE_GATED: snare(ins, r, 1, 0); break;
    case VOICE_SNARE_BRUSH: snare(ins, r, 0, 1); break;
    case VOICE_SNARE_CHIP: snare_chip(ins, r); break;
    case VOICE_CLAP: clap(ins, r); break;
    case VOICE_HAT_CLOSED: hat(ins, r, 0); break;
    case VOICE_HAT_OPEN: hat(ins, r, 1); break;
    case VOICE_HAT_CHIP: hat_chip(ins, r); break;
    case VOICE_SHAKER: shaker(ins, r); break;
    case VOICE_RIM: rim(ins, r); break;
    case VOICE_TOM: tom(ins, r); break;
    case VOICE_CRASH: cymbal(ins, r, 0); break;
    case VOICE_RIDE: cymbal(ins, r, 1); break;
    case VOICE_BREAK_KICK: break_hit(ins, r, 0); break;
    case VOICE_BREAK_SNARE: break_hit(ins, r, 1); break;
    case VOICE_BASS_SUB: bass_sub(ins, r, bpm); break;
    case VOICE_BASS_SAW: bass_saw(ins, r, bpm); break;
    case VOICE_BASS_FM: bass_fm(ins, r, bpm); break;
    case VOICE_BASS_REESE: bass_reese(ins, r, bpm); break;
    case VOICE_BASS_CHIP: bass_chip(ins, r, bpm); break;
    case VOICE_PAD_SUPERSAW: pad_supersaw(ins, r, bpm); break;
    case VOICE_PAD_WARM: pad_warm(ins, r, bpm); break;
    case VOICE_PAD_CHOIR: pad_choir(ins, r, bpm); break;
    case VOICE_PAD_GLASS: pad_glass(ins, r, bpm); break;
    case VOICE_KEYS_EPIANO: keys_epiano(ins, r, bpm); break;
    case VOICE_KEYS_ORGAN: keys_organ(ins, r, bpm); break;
    case VOICE_LEAD_SAW: lead_saw(ins, r, bpm); break;
    case VOICE_LEAD_SQUARE: lead_square(ins, r, bpm); break;
    case VOICE_LEAD_SINE: lead_sine(ins, r, bpm); break;
    case VOICE_LEAD_CHIP: lead_chip(ins, r, bpm); break;
    case VOICE_ARP_PLUCK: arp_pluck(ins, r, bpm); break;
    case VOICE_ARP_CHIP: arp_chip(ins, r, bpm); break;
    case VOICE_PLUCK_KS: pluck_ks(ins, r, bpm); break;
    case VOICE_HARP: harp(ins, r, bpm); break;
    case VOICE_BELL_FM: bell_fm(ins, r, bpm); break;
    case VOICE_CELESTA: celesta(ins, r, bpm, 0); break;
    case VOICE_MUSICBOX: celesta(ins, r, bpm, 1); break;
    case VOICE_FX_RISER: riser(ins, r, bpm, 0); break;
    case VOICE_FX_DOWNLIFTER: riser(ins, r, bpm, 1); break;
    case VOICE_FX_IMPACT: impact(ins, r); break;
    case VOICE_FX_REVERSE_CRASH: reverse_crash(ins, r, bpm); break;
    case VOICE_FX_CRACKLE: crackle(ins, r); break;
    case VOICE_FX_WIND: wind(ins, r); break;
    default: return -1;
    }
    return ins->sample_count > 0 ? 0 : -1;
}
