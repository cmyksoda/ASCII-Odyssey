// SPDX-License-Identifier: GPL-3.0-only
#ifndef WORDS_H
#define WORDS_H

#include <stdint.h>

// A friendly two-word seed, like "velvet comet", picked from entropy the platform supplies.
void seed_random(uint64_t entropy, char *out, int size);

#endif
