// SPDX-License-Identifier: GPL-3.0-or-later
#include "rng.h"

#include <ctype.h>

void seed_normalize(const char *text, char *out, int size) {
    int n = 0, space = 0;
    for (; *text && n < size - 1; text++) {
        unsigned char c = (unsigned char)*text;
        if (isspace(c)) {
            space = n > 0;
            continue;
        }
        if (c < 32 || c > 126) continue;
        if (space && n < size - 2) out[n++] = ' ';
        space = 0;
        out[n++] = (char)tolower(c);
    }
    out[n] = 0;
}

uint64_t seed_hash(const char *text) {
    char normal[SEED_MAX * 4 + 1];
    seed_normalize(text, normal, sizeof normal);
    uint64_t h = 0xcbf29ce484222325ull;
    for (const char *p = normal; *p; p++) {
        h ^= (unsigned char)*p;
        h *= 0x100000001b3ull;
    }
    return h;
}

static uint64_t splitmix(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

void rng_init(Rng *r, uint64_t seed, const char *stream) {
    uint64_t x = seed;
    for (const char *p = stream; p && *p; p++) x = (x ^ (unsigned char)*p) * 0x100000001b3ull;
    for (int i = 0; i < 4; i++) r->s[i] = splitmix(&x);
}

static uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

uint64_t rng_next(Rng *r) {
    uint64_t *s = r->s;
    uint64_t result = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

uint32_t rng_u32(Rng *r) {
    return (uint32_t)(rng_next(r) >> 32);
}

int rng_range(Rng *r, int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(((uint64_t)rng_u32(r) * (uint64_t)(hi - lo)) >> 32);
}

float rng_float(Rng *r) {
    return (float)(rng_next(r) >> 40) * (1.0f / 16777216.0f);
}

float rng_between(Rng *r, float lo, float hi) {
    return lo + (hi - lo) * rng_float(r);
}

int rng_chance(Rng *r, int percent) {
    return rng_range(r, 0, 100) < percent;
}

int rng_weighted(Rng *r, const int *weights, int count) {
    int total = 0;
    for (int i = 0; i < count; i++) total += weights[i] > 0 ? weights[i] : 0;
    if (total <= 0) return 0;
    int pick = rng_range(r, 0, total);
    for (int i = 0; i < count; i++) {
        if (weights[i] <= 0) continue;
        if (pick < weights[i]) return i;
        pick -= weights[i];
    }
    return count - 1;
}

uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float hash01(uint32_t x) {
    return (float)(hash_u32(x) >> 8) * (1.0f / 16777216.0f);
}
