// SPDX-License-Identifier: GPL-3.0-only
#ifndef SONG_H
#define SONG_H

#include <stdint.h>

#define AUDIO_RATE 48000
#define SONG_CHANNELS 24
#define PATTERN_ROWS 64
#define MAX_PATTERNS 64
#define MAX_ORDER 160
#define MAX_INSTRUMENTS 32
#define MAX_SAMPLES_PER_INSTRUMENT 6
#define ENVELOPE_POINTS 12

// Notes count semitones from C-0 = 1, so C-4 (middle C) is 49 and A-4 (440 Hz) is 58.
enum { NOTE_NONE = 0, NOTE_OFF = 121, NOTE_CUT = 122 };

// Effect commands, named after their FastTracker 2 letters.
enum {
    FX_NONE,
    FX_ARPEGGIO,        // 0xy: +x and +y semitones, cycling every tick
    FX_PORTA_UP,        // 1xx
    FX_PORTA_DOWN,      // 2xx
    FX_TONE_PORTA,      // 3xx: glide to the note
    FX_VIBRATO,         // 4xy: speed x, depth y
    FX_VOLUME_SLIDE,    // Axy: up x or down y per tick
    FX_SET_PAN,         // 8xx: 0 left, 128 center, 255 right
    FX_SAMPLE_OFFSET,   // 9xx: start xx * 256 frames in
    FX_RETRIGGER,       // E9x: every x ticks
    FX_NOTE_CUT,        // ECx: silence after x ticks
    FX_NOTE_DELAY,      // EDx: start the note x ticks late
    FX_FILTER,          // Zxx (from Impulse Tracker): low-pass cutoff, 0 closed to 255 open
    FX_WOBBLE,          // Wxx: the filter LFO's rate in quarter cycles per beat, locked to the song (0 holds still)
    FX_COUNT
};

typedef struct {
    uint8_t note, instrument, volume, effect, param;
} PatternCell;

typedef struct {
    PatternCell cells[PATTERN_ROWS][SONG_CHANNELS];
} Pattern;

// Float frames: converting 16-bit samples costs the Wii's PowerPC more than the whole mix does.
typedef struct {
    float *frames;
    int length;
    // No loop when loop_length is 0.
    int loop_start, loop_length;
    // The note that plays this sample at its own rate.
    float root_note;
} Sample;

typedef struct {
    // Ticks and levels (0..1); sustain holds at a point until the note is released.
    int count, sustain;
    uint16_t tick[ENVELOPE_POINTS];
    float level[ENVELOPE_POINTS];
} Envelope;

// What an instrument is for, so the visuals know a kick from a lead.
typedef enum {
    ROLE_NONE,
    ROLE_KICK,
    ROLE_SNARE,
    ROLE_HAT,
    ROLE_PERC,
    ROLE_CRASH,
    ROLE_BASS,
    ROLE_PAD,
    ROLE_CHORD,
    ROLE_ARP,
    ROLE_LEAD,
    ROLE_BELL,
    ROLE_PLUCK,
    ROLE_FX,
    ROLE_COUNT
} Role;

typedef struct {
    char name[24];
    Role role;
    int sample_count;
    Sample samples[MAX_SAMPLES_PER_INSTRUMENT];
    // Highest note each sample covers, lowest sample first.
    uint8_t sample_top[MAX_SAMPLES_PER_INSTRUMENT];
    Envelope volume_env;
    // Volume lost per tick after release, as a fraction of full.
    float fadeout;
    float volume, pan;
    float send_reverb, send_delay;
    // Pumped by the kick's sidechain: 0 not at all, 1 fully, 2 at half depth.
    int ducked;
    // Low-pass cutoff as a 0..1 knob (0 = off), with resonance, and an envelope that opens it on each note.
    float cutoff, resonance, cutoff_env, cutoff_decay;
    float vibrato_depth, vibrato_rate;
    // How far the beat-locked LFO swings the cutoff knob, and its shape: 0 sine, 1 falling saw, 2 triangle.
    float wobble_depth;
    int wobble_shape;
    // A brostep growl, 0 to 1: after the low-pass, the sound is driven and then talks through vowel formants that
    // ride the same LFO, which puts its weight in the mids where small speakers can still play it.
    float growl;
} Instrument;

typedef struct {
    float reverb_size, reverb_damping, reverb_mix;
    float delay_beats, delay_feedback, delay_mix;
    float duck_depth, duck_release;
    float drive;
    // Gain into the master chain (how hard it works), and after it (how loud the result is).
    float pre_gain, master_gain;
} MixSettings;

typedef struct {
    int bpm, speed;
    // Rows in every pattern: 64 for four bars of 4/4 in sixteenths, 48 for four bars of 3/4.
    int pattern_rows;
    int channels;
    int order_length;
    uint8_t order[MAX_ORDER];
    int pattern_count;
    Pattern *patterns;
    int instrument_count;
    Instrument instruments[MAX_INSTRUMENTS];
    MixSettings mix;
    // Per row of the whole song: how far the duck bus pumps (0..1) and a master low-pass sweep (0..1, 1 open).
    int automation_rows;
    float *duck_amount, *master_cutoff;
    // Per row: the section's place in the song's dynamics, as a gain after the glue compressor (1 at a drop).
    float *level;
} Song;

Song *song_create(void);
void song_free(Song *s);
Pattern *song_pattern(Song *s, int index);
int song_rows(const Song *s);
double song_row_seconds(const Song *s);

#endif
