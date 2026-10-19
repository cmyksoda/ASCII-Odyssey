// SPDX-License-Identifier: GPL-3.0-only
#ifndef PROF_H
#define PROF_H

// Named timing zones for hardware test runs; free when no clock is set.
void prof_clock(double (*now_us)(void));
void prof_push(const char *zone);
void prof_pop(void);
// Average milliseconds per zone since the last report, then starts over.
void prof_report(char *out, int size);

#endif
