// SPDX-License-Identifier: GPL-3.0-only
#ifndef SYNTH_H
#define SYNTH_H

#include "rng.h"
#include "song.h"

// Instrument recipes. Each bakes its samples once, at compose time, with the seed choosing the details,
// so playback is plain tracker sample mixing.
typedef enum {
    VOICE_NONE,
    VOICE_KICK,
    VOICE_KICK_SOFT,
    VOICE_KICK_CHIP,
    VOICE_SNARE,
    VOICE_SNARE_GATED,
    VOICE_SNARE_BRUSH,
    VOICE_SNARE_CHIP,
    VOICE_CLAP,
    VOICE_HAT_CLOSED,
    VOICE_HAT_OPEN,
    VOICE_HAT_CHIP,
    VOICE_SHAKER,
    VOICE_RIM,
    VOICE_TOM,
    VOICE_CRASH,
    VOICE_RIDE,
    VOICE_BREAK_KICK,
    VOICE_BREAK_SNARE,
    VOICE_BASS_SUB,
    VOICE_BASS_SAW,
    VOICE_BASS_FM,
    VOICE_BASS_REESE,
    VOICE_BASS_CHIP,
    VOICE_PAD_SUPERSAW,
    VOICE_PAD_WARM,
    VOICE_PAD_CHOIR,
    VOICE_PAD_GLASS,
    VOICE_KEYS_EPIANO,
    VOICE_KEYS_ORGAN,
    VOICE_STAB_SAW,
    VOICE_STAB_DUB,
    VOICE_LEAD_FM,
    VOICE_LEAD_VOX,
    VOICE_PAD_STRINGS,
    VOICE_PAD_FM,
    VOICE_BASS_ACID,
    VOICE_BASS_SQUARE,
    VOICE_MALLET,
    VOICE_VIBES,
    VOICE_BASS_WOBBLE,
    VOICE_LEAD_SAW,
    VOICE_LEAD_SQUARE,
    VOICE_LEAD_SINE,
    VOICE_LEAD_CHIP,
    VOICE_ARP_PLUCK,
    VOICE_ARP_CHIP,
    VOICE_PLUCK_KS,
    VOICE_HARP,
    VOICE_BELL_FM,
    VOICE_CELESTA,
    VOICE_MUSICBOX,
    VOICE_FX_RISER,
    VOICE_FX_DOWNLIFTER,
    VOICE_FX_IMPACT,
    VOICE_FX_REVERSE_CRASH,
    VOICE_FX_CRACKLE,
    VOICE_FX_WIND,
    VOICE_FLUTE,
    VOICE_BASS_SLAP,
    VOICE_ORCH_HIT
} VoiceKind;

// tempo lets risers and gates fit the song's bars.
int synth_instrument(Instrument *ins, VoiceKind kind, Rng *r, float bpm);

#endif
