// SPDX-License-Identifier: GPL-3.0-only
#include "noise.h"

#include "mathx.h"
#include "rng.h"

#define CELLS 8
#define SCALE (NOISE_SIZE / CELLS)

static float fbm_tex[NOISE_SIZE * NOISE_SIZE];
static float value_tex[NOISE_SIZE * NOISE_SIZE];

static float lattice(int x, int y, int period, uint32_t salt) {
    x = ((x % period) + period) % period;
    y = ((y % period) + period) % period;
    return hash01((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ salt);
}

static float smooth_value(float x, float y, int period, uint32_t salt) {
    int ix = mfloor_i(x), iy = mfloor_i(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = lattice(ix, iy, period, salt), b = lattice(ix + 1, iy, period, salt);
    float c = lattice(ix, iy + 1, period, salt), d = lattice(ix + 1, iy + 1, period, salt);
    return mixf(mixf(a, b, fx), mixf(c, d, fx), fy);
}

void noise_init(void) {
    for (int y = 0; y < NOISE_SIZE; y++)
        for (int x = 0; x < NOISE_SIZE; x++) {
            float u = (float)x / NOISE_SIZE, v = (float)y / NOISE_SIZE;
            float sum = 0, amp = 0.5f, norm = 0;
            int period = CELLS;
            for (int o = 0; o < 4; o++) {
                sum += amp * smooth_value(u * (float)period, v * (float)period, period, 0x51ed270bu * (uint32_t)(o + 1));
                norm += amp;
                amp *= 0.5f;
                period *= 2;
            }
            fbm_tex[y * NOISE_SIZE + x] = sum / norm;
            value_tex[y * NOISE_SIZE + x] = smooth_value(u * CELLS * 2, v * CELLS * 2, CELLS * 2, 0x2545f491u);
        }
}

static float sample(const float *tex, float x, float y) {
    x *= SCALE;
    y *= SCALE;
    int ix = mfloor_i(x), iy = mfloor_i(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    int x0 = ix & (NOISE_SIZE - 1), y0 = iy & (NOISE_SIZE - 1);
    int x1 = (x0 + 1) & (NOISE_SIZE - 1), y1 = (y0 + 1) & (NOISE_SIZE - 1);
    float a = tex[y0 * NOISE_SIZE + x0], b = tex[y0 * NOISE_SIZE + x1];
    float c = tex[y1 * NOISE_SIZE + x0], d = tex[y1 * NOISE_SIZE + x1];
    return mixf(mixf(a, b, fx), mixf(c, d, fx), fy);
}

float noise_fbm(float x, float y) {
    return sample(fbm_tex, x, y);
}

float noise_value(float x, float y) {
    return sample(value_tex, x, y);
}
