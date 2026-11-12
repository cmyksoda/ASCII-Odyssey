// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WORDS_H
#define WORDS_H

#include <stdint.h>

// A two-word seed, like "violet lagoon", picked from entropy the platform supplies.
void seed_random(uint64_t entropy, char *out, int size);

#endif
