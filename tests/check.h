// SPDX-License-Identifier: GPL-3.0-only
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>

// Each test is a program of its own, so each keeps its own count.
static int failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

#endif
