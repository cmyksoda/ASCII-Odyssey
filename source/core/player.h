// SPDX-License-Identifier: GPL-3.0-only
#ifndef PLAYER_H
#define PLAYER_H

#include "dsp.h"
#include "song.h"

#include <stdint.h>

#define GHOST_VOICES 24
#define PLAYER_VOICES (SONG_CHANNELS + GHOST_VOICES)
#define PLAYER_BLOCK 256

typedef struct {
    const Instrument *ins;
    const Sample *smp;
    double pos, step;
    float pitch, velocity, vibrato_phase;
    float gain_l, gain_r, target_l, target_r;
    int env_tick, released, active, owner;
    float fade, volume, pan;
    float cutoff, cutoff_env;
    // Wobble rate in LFO cycles per beat, copied from its channel each tick.
    float wobble;
    int filtered;
    Svf svf, vowel[2];
} Voice;

typedef struct {
    int note, instrument;
    float volume, pan, pitch, target_pitch, cutoff, wobble;
    uint8_t porta, vibrato_speed, vibrato_depth, slide, arpeggio, retrigger;
    float vibrato_phase;
    PatternCell cell;
    Voice *voice;
} Channel;

// Called for every note that starts, at its exact time, so the visuals can follow the score.
typedef void (*NoteListener)(void *context, double seconds, int channel, int note, float velocity,
                             const Instrument *ins);

typedef struct {
    const Song *song;
    int order, row, tick, ended;
    double frames_per_tick, frames_left;
    Channel ch[SONG_CHANNELS];
    Voice voices[PLAYER_VOICES];
    Plate plate;
    PingPong delay;
    Compressor glue;
    Limiter limiter;
    Svf master_l, master_r;
    float duck, duck_release, master_knob, master_level, dc_l, dc_r, dc_in_l, dc_in_r;
    float bus[8][PLAYER_BLOCK];
    uint64_t frame;
    int tail_frames;
    // A dry run only walks the score: no mixing, just note events, a hundred times faster.
    int dry;
    // The most voices ever mixed at once, to catch notes that never let go.
    int peak_voices;
    NoteListener listener;
    void *listener_context;
} Player;

int player_init(Player *p, const Song *s, int dry);
void player_free(Player *p);
// Interleaved stereo; returns fewer frames than asked once the song and its tails have finished.
int player_render(Player *p, float *out, int frames);
double player_seconds(const Player *p);

#endif
