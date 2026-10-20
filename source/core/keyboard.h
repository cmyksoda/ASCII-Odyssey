// SPDX-License-Identifier: GPL-3.0-only
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "rng.h"
#include "screen.h"

// Notes the keyboard asks the platform to chime: key presses climb a pentatonic scale.
enum { CHIME_NONE = -1, CHIME_KEYS = 10, CHIME_DELETE = CHIME_KEYS, CHIME_DICE, CHIME_DONE, CHIME_COUNT };

typedef enum { KEYS_OPEN, KEYS_DONE, KEYS_CANCELED } KeysState;

typedef struct {
    char text[SEED_MAX + 1];
    int len;
    // The selected key: rows 0-3 are characters, row 4 the action keys.
    int row, col;
    float pointer_x, pointer_y;
    double opened;
    KeysState state;
} Keyboard;

void keyboard_open(Keyboard *k, const char *text, double now);
// Returns the chime to play for this frame's key press, or CHIME_NONE.
int keyboard_update(Keyboard *k, const Input *in, const Grid *ui, uint64_t entropy);
void keyboard_draw(const Keyboard *k, Grid *ui, double now);
// The cells the keyboard covers, so the caller can quiet the picture behind them.
void keyboard_extent(const Grid *ui, int *col, int *row, int *w, int *h);
// Renders one chime as mono audio at AUDIO_RATE: key notes climb a pentatonic scale.
void keyboard_chime(int which, float *out, int frames);

#endif
