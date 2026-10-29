// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SCREEN_H
#define SCREEN_H

#include "grid.h"

// What the platform draws each frame: the picture grid, then the UI grid on top.
typedef struct {
    Grid *scene, *ui;
    FrameFx fx;
} Screen;

enum {
    INPUT_UP = 1 << 0,
    INPUT_DOWN = 1 << 1,
    INPUT_LEFT = 1 << 2,
    INPUT_RIGHT = 1 << 3,
    INPUT_CONFIRM = 1 << 4,
    INPUT_BACK = 1 << 5,
    INPUT_PAUSE = 1 << 6,
    INPUT_HOME = 1 << 7,
    INPUT_ALT1 = 1 << 8,
    INPUT_ALT2 = 1 << 9,
    INPUT_VOLUME_UP = 1 << 10,
    INPUT_VOLUME_DOWN = 1 << 11,
    INPUT_QUIT = 1 << 12,
    // The Wii Remote's own D-pad up and down, its only way to turn the music up while it plays.
    INPUT_REMOTE_UP = 1 << 13,
    INPUT_REMOTE_DOWN = 1 << 14,
};

#define INPUT_TYPED_MAX 16

typedef struct {
    unsigned held, pressed;
    int pointer;
    // Pointer position in framebuffer pixels.
    float pointer_x, pointer_y;
    // Characters from a USB keyboard; '\b' is backspace, '\n' is enter.
    char typed[INPUT_TYPED_MAX];
    int typed_count;
} Input;

#endif
