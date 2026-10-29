// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef VIDEO_H
#define VIDEO_H

#include "../core/screen.h"

int video_init(void);
int video_wide(void);
// Returns the retraces since the last frame; more than the interval means this one was late.
int video_draw(const Screen *s);
int video_interval(void);
// Seconds of refreshes so far: a steadier clock than the CPU's for pacing motion.
double video_time(void);
// Show each frame for this many retraces: 2 holds pieces at a steady 30 frames a second.
void video_set_interval(int count);
// Microseconds the last frame spent building GX commands, and waiting for the GPU to finish them.
void video_times(unsigned *build_us, unsigned *gpu_us);
// The framebuffer most recently shown, as YUYV.
const void *video_xfb(int *width, int *height);
void video_shutdown(void);

#endif
