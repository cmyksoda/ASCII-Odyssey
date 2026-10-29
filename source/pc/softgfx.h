// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SOFTGFX_H
#define SOFTGFX_H

#include "../core/screen.h"

#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *rgb;
} Image;

int image_init(Image *im, int w, int h);
void image_free(Image *im);
// Draws a screen the way the Wii does into a 640x480 framebuffer.
void soft_render(const Screen *s, Image *fb);
// What a TV shows: 16:9 frames stretched to their real width, then scaled by a whole number for crisp text.
void soft_present(const Image *fb, int wide, int scale, Image *out);
int png_write(const char *path, const Image *im);

#endif
