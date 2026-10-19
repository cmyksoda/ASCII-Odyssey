// SPDX-License-Identifier: GPL-3.0-only
// A fixed little song for checking the engine and the Wii's audio path by ear and by measurement.
#include "testsong.h"

#include "synth.h"

#include <stdlib.h>
#include <string.h>

static void put(Pattern *p, int row, int ch, int note, int ins, int vol, int fx, int param) {
    PatternCell *c = &p->cells[row][ch];
    c->note = (uint8_t)note;
    c->instrument = (uint8_t)ins;
    c->volume = (uint8_t)vol;
    c->effect = (uint8_t)fx;
    c->param = (uint8_t)param;
}

// C-4 is 49; offsets in semitones from it.
#define N(o) (49 + (o))

enum { I_KICK = 1, I_SNARE, I_HAT, I_OPEN, I_BASS, I_PAD, I_LEAD, I_BELL, I_CRASH, I_ARP };

static void drums(Pattern *p) {
    for (int bar = 0; bar < 4; bar++) {
        int r = bar * 16;
        for (int beat = 0; beat < 4; beat++) put(p, r + beat * 4, 0, N(0), I_KICK, 0, 0, 0);
        put(p, r + 4, 1, N(0), I_SNARE, 0, 0, 0);
        put(p, r + 12, 1, N(0), I_SNARE, 0, 0, 0);
        for (int s = 0; s < 16; s += 2) put(p, r + s, 2, N(0), I_HAT, s % 4 == 2 ? 50 : 30, 0, 0);
        put(p, r + 14, 3, N(0), I_OPEN, 40, 0, 0);
    }
}

static const int ROOTS[4] = {-3, -7, 0, -5};
static const int CHORDS[4][3] = {{-3, 0, 4}, {-7, -3, 0}, {-5, 0, 4}, {-5, -1, 2}};

static void harmony(Pattern *p) {
    for (int bar = 0; bar < 4; bar++) {
        int r = bar * 16, root = ROOTS[bar];
        for (int s = 0; s < 16; s += 2) put(p, r + s, 4, N(root - 24 + (s % 8 == 6 ? 12 : 0)), I_BASS, 0, 0, 0);
        for (int k = 0; k < 3; k++) put(p, r, 5 + k, N(CHORDS[bar][k]), I_PAD, 0, 0, 0);
        // A chip arpeggio running through the chord, two octaves up.
        for (int s = 0; s < 16; s += 4) put(p, r + s, 11, N(CHORDS[bar][0] + 12), I_ARP, 34, FX_ARPEGGIO, 0x37);
    }
}

static void melody(Pattern *p) {
    static const int notes[] = {12, 0, 15, 0, 14, 12, 10, 0, 12, 0, 7, 0, 10, 12, 0, 0, 9, 0, 12, 0, 14, 0, 15, 14,
                                12, 0, 10, 0, 7, 0, 0, 0, 12, 0, 15, 0, 17, 15, 14, 0, 12, 0, 14, 0, 10, 0, 0, 0,
                                7, 0, 10, 12, 14, 0, 12, 0, 10, 0, 9, 0, 7, 0, 0, 0};
    for (int s = 0; s < 64; s++) {
        int n = notes[s];
        if (!n) continue;
        int slide = s > 0 && notes[s - 1] && s % 8 == 4;
        put(p, s, 8, N(n - 3), I_LEAD, 0, slide ? FX_TONE_PORTA : FX_VIBRATO, slide ? 0x30 : 0x54);
        if (s % 16 == 0) put(p, s, 9, N(n - 3 + 12), I_BELL, 40, 0, 0);
    }
}

Song *testsong_create(void) {
    Song *s = song_create();
    if (!s) return NULL;
    s->bpm = 118;
    s->speed = 6;
    s->channels = 12;
    Rng r;
    rng_init(&r, 0x0d155e7ull, "test");
    static const VoiceKind kinds[] = {VOICE_KICK, VOICE_SNARE_GATED, VOICE_HAT_CLOSED, VOICE_HAT_OPEN, VOICE_BASS_SAW,
                                      VOICE_PAD_SUPERSAW, VOICE_LEAD_SAW, VOICE_BELL_FM, VOICE_CRASH, VOICE_ARP_CHIP};
    for (unsigned i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        synth_instrument(&s->instruments[s->instrument_count++], kinds[i], &r, (float)s->bpm);
    Pattern *intro = song_pattern(s, 0), *full = song_pattern(s, 1), *outro = song_pattern(s, 2);
    harmony(intro);
    drums(full);
    harmony(full);
    melody(full);
    put(full, 0, 10, N(0), I_CRASH, 0, 0, 0);
    harmony(outro);
    melody(outro);
    const uint8_t order[] = {0, 1, 1, 2};
    memcpy(s->order, order, sizeof order);
    s->order_length = sizeof order;
    s->mix = (MixSettings){.reverb_size = 0.75f, .reverb_damping = 0.4f, .reverb_mix = 0.8f, .delay_beats = 0.75f,
                           .delay_feedback = 0.38f, .delay_mix = 0.55f, .duck_depth = 0.5f, .duck_release = 0.14f,
                           .drive = 0.15f, .master_gain = 1};
    return s;
}
