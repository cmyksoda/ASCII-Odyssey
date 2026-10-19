// SPDX-License-Identifier: GPL-3.0-only
#ifndef RNG_H
#define RNG_H

#include <stdint.h>

#define SEED_MAX 24

typedef struct {
    uint64_t s[4];
} Rng;

// Lowercase, single spaces, no edge spaces: "  Lunar   RIBBON " and "lunar ribbon" are one seed.
void seed_normalize(const char *text, char *out, int size);
uint64_t seed_hash(const char *text);

// Each subsystem draws from its own stream, so retuning one never reshuffles the others.
void rng_init(Rng *r, uint64_t seed, const char *stream);
uint64_t rng_next(Rng *r);
uint32_t rng_u32(Rng *r);
// Uniform in [lo, hi).
int rng_range(Rng *r, int lo, int hi);
// Uniform in [0, 1).
float rng_float(Rng *r);
float rng_between(Rng *r, float lo, float hi);
int rng_chance(Rng *r, int percent);
int rng_weighted(Rng *r, const int *weights, int count);

uint32_t hash_u32(uint32_t x);
float hash01(uint32_t x);

#endif
