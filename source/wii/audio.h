// SPDX-License-Identifier: GPL-3.0-only
#ifndef AUDIO_H
#define AUDIO_H

#include "../core/player.h"

#include <stdint.h>

typedef struct {
    unsigned restarts, refusals;
    // Mixing time per buffer as a share of the buffer's playing time.
    float load_avg, load_max;
} AudioStatus;

void audio_init(void);
// The producer thread owns the player from here until audio_stop.
int audio_start(Player *p);
void audio_stop(void);
// Frames the TV has actually played, for keeping pictures locked to the music.
uint64_t audio_played(void);
void audio_pause(int paused);
void audio_volume(float level);
// Renders the keyboard's chimes, once the core's math tables are ready, so no key press waits for them.
void audio_chimes_prepare(void);
// Plays one of the keyboard's chimes over whatever else is sounding.
void audio_chime(int which);
void audio_status(AudioStatus *out);

#endif
