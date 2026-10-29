// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MATHX_H
#define MATHX_H

// Deterministic math for everything that decides what a seed looks and sounds like. Built only from
// IEEE +-*/ (with fp-contract off), so the Wii's newlib and the PC's glibc never disagree.

#define PI_F 3.14159265358979f
#define TAU_F 6.28318530717959f

void mathx_init(void);

float msin(float x);
float mcos(float x);
float mexp2(float x);
float mexp(float x);
float mlog2(float x);
float mpow(float base, float exponent);
float mtanh(float x);
float msqrt(float x);
float matan2(float y, float x);
float mhypot(float x, float y);

static inline int mfloor_i(float x) {
    int i = (int)x;
    return (float)i > x ? i - 1 : i;
}

static inline float mfloor(float x) {
    return (float)mfloor_i(x);
}

static inline float mfract(float x) {
    return x - mfloor(x);
}

static inline float mabs(float x) {
    return x < 0 ? -x : x;
}

static inline float mmin(float a, float b) {
    return a < b ? a : b;
}

static inline float mmax(float a, float b) {
    return a > b ? a : b;
}

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

static inline float saturate(float x) {
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

static inline float mixf(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float smoothstep(float e0, float e1, float x) {
    float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3 - 2 * t);
}

static inline float ease_out(float t) {
    t = saturate(t);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

static inline int clampi(int x, int lo, int hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

#endif
