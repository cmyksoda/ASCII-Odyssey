// SPDX-License-Identifier: GPL-3.0-only
#ifndef VIDEO_H
#define VIDEO_H

#include "../core/screen.h"

int video_init(void);
int video_wide(void);
void video_draw(const Screen *s);
// Microseconds the last frame spent building GX commands, and waiting for the GPU to finish them.
void video_times(unsigned *build_us, unsigned *gpu_us);
// The framebuffer most recently shown, as YUYV.
const void *video_xfb(int *width, int *height);
void video_shutdown(void);

#endif
