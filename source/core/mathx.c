// SPDX-License-Identifier: GPL-3.0-only
#include "mathx.h"

#include <stdint.h>

#define SIN_STEPS 4096

static float sin_table[SIN_STEPS + 1];

static double series_sin(double x) {
    const double pi = 3.14159265358979323846;
    while (x > pi) x -= 2 * pi;
    while (x < -pi) x += 2 * pi;
    if (x > pi / 2) x = pi - x;
    if (x < -pi / 2) x = -pi - x;
    double term = x, sum = x, x2 = x * x;
    for (int n = 1; n < 12; n++) {
        term *= -x2 / ((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

void mathx_init(void) {
    for (int i = 0; i <= SIN_STEPS; i++)
        sin_table[i] = (float)series_sin((double)i * (2 * 3.14159265358979323846) / SIN_STEPS);
}

float msin(float x) {
    float t = x * (SIN_STEPS / TAU_F);
    int i = mfloor_i(t);
    float f = t - (float)i;
    int k = i & (SIN_STEPS - 1);
    return sin_table[k] + (sin_table[k + 1] - sin_table[k]) * f;
}

float mcos(float x) {
    return msin(x + PI_F / 2);
}

typedef union {
    float f;
    uint32_t u;
} Bits;

float mexp2(float x) {
    if (x < -126) return 0;
    if (x > 127) x = 127;
    int i = mfloor_i(x);
    float f = x - (float)i;
    // 2^f on [0,1): Taylor of e^(f ln 2) to the 7th power, below float precision.
    float y = f * 0.69314718f;
    float p = 1 + y * (1 + y * (0.5f + y * (0.16666667f + y * (0.041666668f +
              y * (0.0083333338f + y * (0.0013888889f + y * 0.00019841270f))))));
    Bits b = {.u = (uint32_t)(i + 127) << 23};
    return p * b.f;
}

float mexp(float x) {
    return mexp2(x * 1.44269504f);
}

float mlog2(float x) {
    if (x <= 0) return -126;
    Bits b = {.f = x};
    int e = (int)((b.u >> 23) & 255) - 127;
    b.u = (b.u & 0x007fffffu) | 0x3f800000u;
    float m = b.f;
    if (m > 1.41421356f) {
        m *= 0.5f;
        e++;
    }
    float z = (m - 1) / (m + 1), z2 = z * z;
    float ln = 2 * z * (1 + z2 * (0.33333333f + z2 * (0.2f + z2 * (0.14285714f + z2 * 0.11111111f))));
    return (float)e + ln * 1.44269504f;
}

float mpow(float base, float exponent) {
    if (base <= 0) return 0;
    return mexp2(exponent * mlog2(base));
}

float mtanh(float x) {
    if (x > 9) return 1;
    if (x < -9) return -1;
    float e = mexp2(2.88539008f * x);
    return (e - 1) / (e + 1);
}

static float mrsqrt(float x) {
    if (x <= 0) return 0;
    Bits b = {.f = x};
    b.u = 0x5f375a86u - (b.u >> 1);
    float y = b.f, half = 0.5f * x;
    y = y * (1.5f - half * y * y);
    y = y * (1.5f - half * y * y);
    y = y * (1.5f - half * y * y);
    return y;
}

float msqrt(float x) {
    return x <= 0 ? 0 : x * mrsqrt(x);
}

float mhypot(float x, float y) {
    return msqrt(x * x + y * y);
}

float matan2(float y, float x) {
    float ax = mabs(x), ay = mabs(y);
    if (ax == 0 && ay == 0) return 0;
    float a = ax > ay ? ay / ax : ax / ay, s = a * a;
    // Minimax arctangent on [0,1] (the Cg reference polynomial), error near 1e-6 rad.
    float r = -0.013480470f;
    r = r * s + 0.057477314f;
    r = r * s - 0.121239071f;
    r = r * s + 0.195635925f;
    r = r * s - 0.332994597f;
    r = r * s + 0.999995630f;
    r *= a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    return y < 0 ? -r : r;
}
