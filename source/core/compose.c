// SPDX-License-Identifier: GPL-3.0-only
#include "compose.h"

#include "mathx.h"
#include "synth.h"

#include <stdlib.h>
#include <string.h>

const char *const mood_names[MOOD_COUNT] = {"keygen", "ambient", "synthwave", "lo-fi",
                                            "trance", "jungle",  "music box", "dub"};
const char *const section_names[SECTION_KINDS] = {"intro", "verse", "build", "drop", "break", "outro"};

enum {
    CH_KICK,
    CH_SNARE,
    CH_HAT,
    CH_PERC,
    CH_TOM,
    CH_BASS,
    CH_PAD,
    CH_LEAD = CH_PAD + 4,
    CH_ECHO,
    CH_ARP,
    CH_BELL,
    CH_FX,
    CH_TEXTURE
};

typedef enum {
    SL_KICK,
    SL_SNARE,
    SL_CLAP,
    SL_HAT,
    SL_OHAT,
    SL_SHAKER,
    SL_RIDE,
    SL_TOM,
    SL_RIM,
    SL_CRASH,
    SL_BASS,
    SL_PAD,
    SL_KEYS,
    SL_LEAD,
    SL_ARP,
    SL_BELL,
    SL_PLUCK,
    SL_RISER,
    SL_DOWN,
    SL_IMPACT,
    SL_REVERSE,
    SL_TEXTURE,
    SL_COUNT
} Slot;

#define DRUM_NOTE 49
#define NONE (-1)

typedef struct {
    Rng rng;
    Song *song;
    SongInfo *info;
    Mood mood;
    Key key;
    int bars, rows_per_bar, beats_per_bar, bars_per_pattern;
    int swing;
    int slot[SL_COUNT];
    SectionKind kind_of[MAX_BARS];
    float energy_of[MAX_BARS];
    int section_of[MAX_BARS], bar_in_section[MAX_BARS];
    Chord chord_of[MAX_BARS][2];
    int pad_voicing[MAX_BARS][4], pad_voices;
    // The melody's shape: an arch, a climb, a valley or a wave.
    int contour;
} Composer;

/* ---------- Writing into patterns ---------- */

static PatternCell *cell_at(Composer *c, int bar, int row, int ch) {
    if (bar < 0 || bar >= c->bars || row < 0 || row >= c->rows_per_bar) return NULL;
    Pattern *p = song_pattern(c->song, bar / c->bars_per_pattern);
    return &p->cells[(bar % c->bars_per_pattern) * c->rows_per_bar + row][ch];
}

// vol is 1..64, or 0 for the instrument's default.
static void put(Composer *c, int bar, int row, int ch, int note, Slot slot, int vol, int fx, int param) {
    PatternCell *cell = cell_at(c, bar, row, ch);
    if (!cell || !c->slot[slot]) return;
    int pitched = note >= 1 && note <= 120;
    if (pitched && ch >= CH_BASS && c->info->lift && bar >= c->info->lift_bar) note += c->info->lift;
    // Swing pushes every off-beat sixteenth late by a tick or two.
    if (c->swing && pitched && (row & 1) && fx == FX_NONE) {
        fx = FX_NOTE_DELAY;
        param = c->swing;
    }
    cell->note = (uint8_t)note;
    cell->instrument = (uint8_t)c->slot[slot];
    cell->volume = (uint8_t)(vol > 0 ? (vol > 64 ? 64 : vol) + 1 : 0);
    cell->effect = (uint8_t)fx;
    cell->param = (uint8_t)param;
}

static void put_off(Composer *c, int bar, int row, int ch) {
    PatternCell *cell = cell_at(c, bar, row, ch);
    if (cell && !cell->note) cell->note = NOTE_OFF;
}

static int drum(Composer *c, int bar, int row, int ch, Slot slot, int vol) {
    if (!c->slot[slot]) return 0;
    put(c, bar, row, ch, DRUM_NOTE, slot, vol, FX_NONE, 0);
    return 1;
}

static const Chord *chord_at(const Composer *c, int bar, int row) {
    return &c->chord_of[bar][row >= c->rows_per_bar / 2 ? 1 : 0];
}

static int section_last_bar(const Composer *c, int bar) {
    return bar + 1 >= c->bars || c->section_of[bar + 1] != c->section_of[bar];
}

/* ---------- Moods ---------- */

typedef struct {
    int bpm_lo, bpm_hi;
    int swing_lo, swing_hi;
    // Weights per scale.
    int scales[SCALE_COUNT];
    // Bars per chord: 1, or 2 for slow moods.
    int chord_bars;
    // Candidate instruments per slot, ending at VOICE_COUNT; an empty list leaves the slot out.
    VoiceKind voices[SL_COUNT][4];
    MixSettings mix;
    float lift_chance;
} MoodSpec;

#define V(...) {__VA_ARGS__, VOICE_COUNT}
#define NOV {VOICE_COUNT}

static const MoodSpec MOODS[MOOD_COUNT] = {
    [MOOD_KEYGEN] = {.bpm_lo = 125, .bpm_hi = 150, .scales = {[SCALE_MAJOR] = 35, [SCALE_MINOR] = 45, [SCALE_DORIAN] = 20},
                     .chord_bars = 1,
                     .voices = {[SL_KICK] = V(VOICE_KICK_CHIP), [SL_SNARE] = V(VOICE_SNARE_CHIP), [SL_HAT] = V(VOICE_HAT_CHIP),
                                [SL_BASS] = V(VOICE_BASS_CHIP), [SL_LEAD] = V(VOICE_LEAD_CHIP), [SL_ARP] = V(VOICE_ARP_CHIP),
                                [SL_CLAP] = NOV, [SL_OHAT] = NOV, [SL_SHAKER] = NOV, [SL_RIDE] = NOV, [SL_TOM] = NOV,
                                [SL_RIM] = NOV, [SL_CRASH] = NOV, [SL_PAD] = NOV, [SL_KEYS] = NOV, [SL_BELL] = NOV,
                                [SL_PLUCK] = NOV, [SL_RISER] = NOV, [SL_DOWN] = NOV, [SL_IMPACT] = NOV,
                                [SL_REVERSE] = NOV, [SL_TEXTURE] = NOV},
                     .mix = {.reverb_size = 0.5f, .reverb_damping = 0.5f, .reverb_mix = 0.5f, .delay_beats = 0.75f,
                             .delay_feedback = 0.3f, .delay_mix = 0.45f, .duck_depth = 0, .duck_release = 0.1f,
                             .drive = 0.08f, .pre_gain = 0.85f, .master_gain = 0.7f},
                     .lift_chance = 25},
    [MOOD_AMBIENT] = {.bpm_lo = 62, .bpm_hi = 84,
                      .scales = {[SCALE_MAJOR] = 30, [SCALE_LYDIAN] = 30, [SCALE_DORIAN] = 20, [SCALE_MINOR] = 20},
                      .chord_bars = 2,
                      .voices = {[SL_KICK] = V(VOICE_KICK_SOFT), [SL_SNARE] = NOV, [SL_HAT] = NOV,
                                 [SL_SHAKER] = V(VOICE_SHAKER), [SL_BASS] = V(VOICE_BASS_SUB),
                                 [SL_PAD] = V(VOICE_PAD_GLASS, VOICE_PAD_WARM, VOICE_PAD_CHOIR),
                                 [SL_LEAD] = V(VOICE_LEAD_SINE), [SL_ARP] = V(VOICE_HARP, VOICE_PLUCK_KS),
                                 [SL_BELL] = V(VOICE_BELL_FM, VOICE_CELESTA), [SL_TEXTURE] = V(VOICE_FX_WIND),
                                 [SL_CLAP] = NOV, [SL_OHAT] = NOV, [SL_RIDE] = NOV, [SL_TOM] = NOV, [SL_RIM] = NOV,
                                 [SL_CRASH] = NOV, [SL_KEYS] = NOV, [SL_PLUCK] = NOV, [SL_RISER] = NOV,
                                 [SL_DOWN] = NOV, [SL_IMPACT] = NOV, [SL_REVERSE] = NOV},
                      .mix = {.reverb_size = 0.86f, .reverb_damping = 0.45f, .reverb_mix = 1.0f, .delay_beats = 1.5f,
                              .delay_feedback = 0.48f, .delay_mix = 0.55f, .duck_depth = 0, .duck_release = 0.2f,
                              .drive = 0, .pre_gain = 0.6f, .master_gain = 0.9f},
                      .lift_chance = 0},
    [MOOD_SYNTHWAVE] = {.bpm_lo = 92, .bpm_hi = 116,
                        .scales = {[SCALE_MINOR] = 60, [SCALE_DORIAN] = 25, [SCALE_HARMONIC_MINOR] = 15},
                        .chord_bars = 1,
                        .voices = {[SL_KICK] = V(VOICE_KICK), [SL_SNARE] = V(VOICE_SNARE_GATED), [SL_HAT] = V(VOICE_HAT_CLOSED),
                                   [SL_OHAT] = V(VOICE_HAT_OPEN), [SL_TOM] = V(VOICE_TOM), [SL_CRASH] = V(VOICE_CRASH),
                                   [SL_BASS] = V(VOICE_BASS_SAW, VOICE_BASS_FM),
                                   [SL_PAD] = V(VOICE_PAD_SUPERSAW, VOICE_PAD_WARM),
                                   [SL_LEAD] = V(VOICE_LEAD_SAW, VOICE_LEAD_SQUARE), [SL_ARP] = V(VOICE_ARP_PLUCK),
                                   [SL_BELL] = V(VOICE_BELL_FM), [SL_RISER] = V(VOICE_FX_RISER),
                                   [SL_IMPACT] = V(VOICE_FX_IMPACT), [SL_REVERSE] = V(VOICE_FX_REVERSE_CRASH),
                                   [SL_DOWN] = V(VOICE_FX_DOWNLIFTER), [SL_CLAP] = NOV, [SL_SHAKER] = NOV,
                                   [SL_RIDE] = NOV, [SL_RIM] = NOV, [SL_KEYS] = NOV, [SL_PLUCK] = NOV,
                                   [SL_TEXTURE] = NOV},
                        .mix = {.reverb_size = 0.8f, .reverb_damping = 0.42f, .reverb_mix = 0.85f, .delay_beats = 0.75f,
                                .delay_feedback = 0.42f, .delay_mix = 0.6f, .duck_depth = 0.45f, .duck_release = 0.14f,
                                .drive = 0.18f, .pre_gain = 0.9f, .master_gain = 0.8f},
                        .lift_chance = 30},
};

static int implemented(Mood m) {
    return m == MOOD_KEYGEN || m == MOOD_AMBIENT || m == MOOD_SYNTHWAVE;
}

/* ---------- Form ---------- */

typedef struct {
    SectionKind kind;
    int bars;
    float energy;
} Part;

static const Part FORM_EDM[] = {{SECTION_INTRO, 8, 0.25f}, {SECTION_VERSE, 16, 0.5f}, {SECTION_BUILD, 8, 0.7f},
                                {SECTION_DROP, 16, 1.0f},  {SECTION_BREAK, 8, 0.35f}, {SECTION_BUILD, 8, 0.75f},
                                {SECTION_DROP, 16, 1.0f},  {SECTION_OUTRO, 8, 0.3f}};
static const Part FORM_CHIP[] = {{SECTION_INTRO, 8, 0.3f},  {SECTION_VERSE, 16, 0.6f}, {SECTION_DROP, 16, 0.9f},
                                 {SECTION_BREAK, 8, 0.4f},  {SECTION_VERSE, 8, 0.6f},  {SECTION_DROP, 16, 1.0f},
                                 {SECTION_OUTRO, 8, 0.3f}};
static const Part FORM_AMBIENT[] = {{SECTION_INTRO, 8, 0.2f},  {SECTION_VERSE, 16, 0.4f}, {SECTION_DROP, 16, 0.6f},
                                    {SECTION_BREAK, 8, 0.3f},  {SECTION_DROP, 16, 0.65f}, {SECTION_OUTRO, 8, 0.2f}};

static void plan_form(Composer *c, LengthPref length) {
    const Part *form;
    int count;
    switch (c->mood) {
    case MOOD_KEYGEN:
        form = FORM_CHIP;
        count = (int)(sizeof FORM_CHIP / sizeof FORM_CHIP[0]);
        break;
    case MOOD_AMBIENT:
        form = FORM_AMBIENT;
        count = (int)(sizeof FORM_AMBIENT / sizeof FORM_AMBIENT[0]);
        break;
    default:
        form = FORM_EDM;
        count = (int)(sizeof FORM_EDM / sizeof FORM_EDM[0]);
        break;
    }
    static const float targets[3] = {80, 150, 230};
    float bar_seconds = (float)c->beats_per_bar * 60.0f / (float)c->info->bpm;
    int natural = 0;
    for (int i = 0; i < count; i++) natural += form[i].bars;
    float scale = targets[length] / bar_seconds / (float)natural;
    SongInfo *info = c->info;
    info->section_count = 0;
    int bar = 0;
    for (int i = 0; i < count && info->section_count < MAX_SECTIONS; i++) {
        // Short pieces drop their first verse and break; every section stays a whole number of 4-bar phrases.
        if (length == LENGTH_SHORT && (form[i].kind == SECTION_BREAK || (form[i].kind == SECTION_VERSE && i == 1))) continue;
        int bars = (int)((float)form[i].bars * scale / 4 + 0.5f) * 4;
        if (bars < 4) bars = 4;
        if (form[i].kind == SECTION_BUILD && bars > 8) bars = 8;
        if (bar + bars > MAX_BARS) break;
        Section *s = &info->sections[info->section_count++];
        s->kind = form[i].kind;
        s->start_bar = bar;
        s->bars = bars;
        s->energy = form[i].energy;
        for (int b = bar; b < bar + bars; b++) {
            c->kind_of[b] = s->kind;
            c->energy_of[b] = s->energy;
            c->section_of[b] = info->section_count - 1;
            c->bar_in_section[b] = b - bar;
        }
        bar += bars;
    }
    c->bars = bar;
}

/* ---------- Harmony ---------- */

static const int PROG_MINOR[][4] = {{0, 5, 2, 6}, {0, 3, 5, 4}, {0, 6, 5, 6}, {5, 6, 0, 0}, {0, 5, 3, 4},
                                    {0, 2, 6, 5}, {3, 0, 5, 4}, {0, 5, 6, 4}, {0, 3, 6, 2}, {5, 3, 0, 6}};
static const int PROG_MAJOR[][4] = {{0, 4, 5, 3}, {0, 5, 3, 4}, {3, 4, 2, 5}, {0, 2, 3, 4}, {5, 3, 0, 4},
                                    {0, 3, 0, 4}, {0, 3, 5, 4}, {3, 0, 4, 5}, {0, 5, 1, 4}};
static const int PROG_LYDIAN[][4] = {{0, 1, 0, 1}, {0, 1, 4, 1}, {0, 4, 1, 0}, {5, 1, 0, 0}, {0, 2, 1, 0}};

static void pick_progression(Composer *c, int *out) {
    const int(*bank)[4];
    int n;
    if (c->key.scale == SCALE_LYDIAN && rng_chance(&c->rng, 60)) {
        bank = PROG_LYDIAN;
        n = (int)(sizeof PROG_LYDIAN / sizeof PROG_LYDIAN[0]);
    } else if (c->key.scale == SCALE_MAJOR || c->key.scale == SCALE_LYDIAN || c->key.scale == SCALE_MIXOLYDIAN) {
        bank = PROG_MAJOR;
        n = (int)(sizeof PROG_MAJOR / sizeof PROG_MAJOR[0]);
    } else {
        bank = PROG_MINOR;
        n = (int)(sizeof PROG_MINOR / sizeof PROG_MINOR[0]);
    }
    memcpy(out, bank[rng_range(&c->rng, 0, n)], sizeof(int) * 4);
}

static Extension pick_extension(Composer *c, int degree) {
    int r = rng_range(&c->rng, 0, 100);
    switch (c->mood) {
    case MOOD_AMBIENT:
        if (r < 30) return EXT_ADD9;
        if (r < 50) return EXT_SEVENTH;
        if (r < 65) return EXT_SUS2;
        return EXT_TRIAD;
    case MOOD_SYNTHWAVE:
        if (r < 20 && degree != 4) return EXT_ADD9;
        if (r < 30) return EXT_SEVENTH;
        return EXT_TRIAD;
    default: return EXT_TRIAD;
    }
}

static int minor_family(Scale s) {
    return s == SCALE_MINOR || s == SCALE_DORIAN || s == SCALE_PHRYGIAN || s == SCALE_HARMONIC_MINOR;
}

// Diminished triads sour a pop progression: minor keys borrow the natural-minor chord instead,
// major keys swap in the chord a third below, which shares two of its notes.
static Chord consonant_chord(Key k, int degree, Extension e) {
    Chord ch = chord_build(k, degree, e);
    if (ch.tones[2] != 6) return ch;
    if (minor_family(k.scale)) {
        Key natural = {k.tonic, SCALE_MINOR};
        ch = chord_build(natural, degree, e);
        return ch.tones[2] != 6 ? ch : chord_build(natural, 3, e);
    }
    return chord_build(k, (degree + 5) % 7, e);
}

static void plan_harmony(Composer *c) {
    int main_prog[4], alt_prog[4];
    pick_progression(c, main_prog);
    pick_progression(c, alt_prog);
    Extension ext_main[4], ext_alt[4];
    for (int i = 0; i < 4; i++) {
        ext_main[i] = pick_extension(c, main_prog[i]);
        ext_alt[i] = pick_extension(c, alt_prog[i]);
    }
    int chord_bars = MOODS[c->mood].chord_bars;
    int minor = minor_family(c->key.scale);
    for (int bar = 0; bar < c->bars; bar++) {
        int alt = c->kind_of[bar] == SECTION_BREAK;
        const int *prog = alt ? alt_prog : main_prog;
        const Extension *ext = alt ? ext_alt : ext_main;
        int i = (c->bar_in_section[bar] / chord_bars) % 4;
        int degree = prog[i];
        Extension e = ext[i];
        // The last bar of a build leans on the dominant (or the flat seventh in minor) to pull into the drop.
        if (c->kind_of[bar] == SECTION_BUILD && section_last_bar(c, bar)) {
            degree = minor ? (rng_chance(&c->rng, 50) ? 6 : 4) : 4;
            e = EXT_SUS4;
        }
        // The very last bar comes home.
        if (bar == c->bars - 1) {
            degree = 0;
            e = c->mood == MOOD_AMBIENT ? EXT_ADD9 : EXT_TRIAD;
        }
        Key k = c->key;
        if (k.scale == SCALE_HARMONIC_MINOR && degree != 4 && degree != 6) k.scale = SCALE_MINOR;
        Chord ch = consonant_chord(k, degree, e);
        c->chord_of[bar][0] = c->chord_of[bar][1] = ch;
    }
    SongInfo *info = c->info;
    info->chord_count = 0;
    for (int bar = 0; bar < c->bars; bar++) {
        if (bar > 0 && c->chord_of[bar][0].root_pc == c->chord_of[bar - 1][1].root_pc &&
            c->chord_of[bar][0].ext == c->chord_of[bar - 1][1].ext)
            continue;
        ChordMark *m = &info->chords[info->chord_count++];
        m->bar = bar;
        m->time = bar * info->bar_seconds;
        m->chord = c->chord_of[bar][0];
        if (info->lift && bar >= info->lift_bar) m->chord.root_pc = (m->chord.root_pc + info->lift) % 12;
    }
}

/* ---------- Instruments ---------- */

static void make_instruments(Composer *c) {
    const MoodSpec *spec = &MOODS[c->mood];
    Song *s = c->song;
    for (int sl = 0; sl < SL_COUNT; sl++) {
        int n = 0;
        while (n < 4 && spec->voices[sl][n] != VOICE_COUNT) n++;
        if (n == 0 || s->instrument_count >= MAX_INSTRUMENTS) continue;
        VoiceKind kind = spec->voices[sl][rng_range(&c->rng, 0, n)];
        Rng voice_rng;
        rng_init(&voice_rng, rng_next(&c->rng), "voice");
        if (synth_instrument(&s->instruments[s->instrument_count], kind, &voice_rng, (float)s->bpm) == 0)
            c->slot[sl] = ++s->instrument_count;
    }
}

/* ---------- Drums ---------- */

// One bar of hits, one character per step: X accent, x normal, o ghost, . rest.
static void drum_line(Composer *c, int bar, int ch, Slot slot, const char *pattern, int volume) {
    for (int row = 0; pattern[row] && row < c->rows_per_bar; row++) {
        int v = pattern[row] == 'X' ? volume : pattern[row] == 'x' ? volume * 3 / 4 : pattern[row] == 'o' ? volume * 2 / 5 : 0;
        if (v) drum(c, bar, row, ch, slot, v);
    }
}

static void snare_roll(Composer *c, int bar, int from_row, int to_row, int step, int vol_lo, int vol_hi) {
    for (int row = from_row; row < to_row; row += step) {
        int v = vol_lo + (vol_hi - vol_lo) * (row - from_row) / (to_row - from_row > 0 ? to_row - from_row : 1);
        drum(c, bar, row, CH_SNARE, SL_SNARE, v);
    }
}

static void tom_fill(Composer *c, int bar) {
    if (!c->slot[SL_TOM]) return;
    static const int notes[4] = {56, 52, 49, 45};
    for (int i = 0; i < 4; i++) put(c, bar, 8 + i * 2, CH_TOM, notes[i], SL_TOM, 52 - i * 4, FX_NONE, 0);
}

static void drums_build(Composer *c, int bar) {
    int pos = c->bar_in_section[bar], length = c->info->sections[c->section_of[bar]].bars, from_end = length - pos;
    float rise = (float)pos / (float)length;
    int v = 26 + (int)(rise * 34);
    if (from_end > 4)
        drum_line(c, bar, CH_SNARE, SL_SNARE, "x...x...x...x...", v);
    else if (from_end > 2)
        drum_line(c, bar, CH_SNARE, SL_SNARE, "x.x.x.x.x.x.x.x.", v);
    else if (from_end > 1)
        snare_roll(c, bar, 0, 16, 1, v - 6, v + 2);
    else {
        // The last bar: a 32nd-note roll that stops dead half way, leaving a breath before the drop.
        snare_roll(c, bar, 0, 8, 1, v, 62);
        for (int row = 0; row < 8; row += 2) {
            PatternCell *cell = cell_at(c, bar, row, CH_SNARE);
            if (cell) {
                cell->effect = FX_RETRIGGER;
                cell->param = 3;
            }
        }
    }
    if (from_end > 1) drum_line(c, bar, CH_KICK, SL_KICK, "x...x...x...x...", 50 + (int)(rise * 12));
    if (from_end > 2) drum_line(c, bar, CH_HAT, SL_HAT, "..x...x...x...x.", 30);
}

static void drums_synthwave(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar];
    int last = section_last_bar(c, bar), phrase_end = (pos % 8) == 7;
    switch (k) {
    case SECTION_INTRO:
        if (pos >= 4) drum_line(c, bar, CH_HAT, SL_HAT, "x.x.x.x.x.x.x.x.", 26);
        if (last) tom_fill(c, bar);
        break;
    case SECTION_VERSE:
    case SECTION_DROP:
    case SECTION_OUTRO: {
        int full = k == SECTION_DROP;
        drum_line(c, bar, CH_KICK, SL_KICK, full ? "x...x...x...x..." : "x.......x.......", 60);
        drum_line(c, bar, CH_SNARE, SL_SNARE, phrase_end && full ? "....x.......x.xx" : "....x.......x...", 54);
        drum_line(c, bar, CH_HAT, SL_HAT, full ? "x.X.x.X.x.X.x.X." : "x.x.x.x.x.x.x.x.", 36);
        if (full && pos % 2 == 1) drum_line(c, bar, CH_PERC, SL_OHAT, "..............x.", 40);
        if (phrase_end && !full && !last) tom_fill(c, bar);
        if (k == SECTION_OUTRO && pos >= c->info->sections[c->section_of[bar]].bars - 2) {
            PatternCell *cell = cell_at(c, bar, 0, CH_HAT);
            if (cell) cell->note = 0;
        }
        break;
    }
    case SECTION_BUILD: drums_build(c, bar); break;
    case SECTION_BREAK:
        if (pos >= 4) drum_line(c, bar, CH_HAT, SL_HAT, "x...x...x...x...", 24);
        break;
    default: break;
    }
}

static void drums_keygen(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], phrase_end = (pos % 4) == 3, last = section_last_bar(c, bar);
    switch (k) {
    case SECTION_INTRO:
        drum_line(c, bar, CH_HAT, SL_HAT, "x.x.x.x.x.x.x.x.", 34);
        if (pos >= 4) drum_line(c, bar, CH_KICK, SL_KICK, "x.......x.......", 56);
        if (last) snare_roll(c, bar, 8, 16, 1, 30, 56);
        break;
    case SECTION_VERSE:
    case SECTION_DROP:
        drum_line(c, bar, CH_KICK, SL_KICK, k == SECTION_DROP ? "x.....x.x.....x." : "x.......x.......", 60);
        drum_line(c, bar, CH_SNARE, SL_SNARE, phrase_end ? "....x.......x.xx" : "....x.......x...", 52);
        drum_line(c, bar, CH_HAT, SL_HAT, k == SECTION_DROP ? "xxXxxxXxxxXxxxXx" : "x.X.x.X.x.X.x.X.", 30);
        if (last && k == SECTION_VERSE) snare_roll(c, bar, 8, 16, 1, 34, 60);
        break;
    case SECTION_BREAK: drum_line(c, bar, CH_HAT, SL_HAT, "x...x...x...x...", 28); break;
    case SECTION_OUTRO:
        drum_line(c, bar, CH_KICK, SL_KICK, "x.......x.......", 52);
        drum_line(c, bar, CH_HAT, SL_HAT, "x.x.x.x.x.x.x.x.", 26);
        break;
    default: break;
    }
}

static void drums_ambient(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    if (k == SECTION_DROP) {
        drum_line(c, bar, CH_KICK, SL_KICK, "x...............", 44);
        drum_line(c, bar, CH_PERC, SL_SHAKER, "..x...x...x...x.", 26);
    } else if (k == SECTION_VERSE && c->bar_in_section[bar] >= 8) {
        drum_line(c, bar, CH_PERC, SL_SHAKER, "......x.......x.", 18);
    }
}

/* ---------- Bass ---------- */

static int bass_note(const Chord *ch) {
    // Roots live between E1 and D#2.
    int n = 1 + 12 + ch->root_pc;
    if (n < 17) n += 12;
    return n;
}

static void bass_part(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int resting = (k == SECTION_INTRO && c->bar_in_section[bar] < 4) || k == SECTION_BREAK;
    if (resting && c->mood != MOOD_AMBIENT) {
        // A looping bass sample would otherwise drone on through the rest.
        put_off(c, bar, 0, CH_BASS);
        return;
    }
    const Chord *ch = chord_at(c, bar, 0);
    int root = bass_note(ch);
    int next_root = bar + 1 < c->bars ? bass_note(&c->chord_of[bar + 1][0]) : root;
    switch (c->mood) {
    case MOOD_AMBIENT:
        if (c->bar_in_section[bar] % MOODS[c->mood].chord_bars == 0) put(c, bar, 0, CH_BASS, root, SL_BASS, 52, FX_NONE, 0);
        break;
    case MOOD_KEYGEN: {
        static const int shape[8] = {0, 12, 0, 12, 7, 12, 0, 12};
        for (int i = 0; i < 8; i++) put(c, bar, i * 2, CH_BASS, root + 12 + shape[i], SL_BASS, i % 2 ? 40 : 56, FX_NONE, 0);
        break;
    }
    default: {
        int driving = k == SECTION_DROP || (k == SECTION_BUILD && !section_last_bar(c, bar));
        if (k == SECTION_BUILD && section_last_bar(c, bar)) {
            put(c, bar, 0, CH_BASS, root, SL_BASS, 56, FX_NONE, 0);
            put(c, bar, 8, CH_BASS, NOTE_CUT, SL_BASS, 0, FX_NONE, 0);
            break;
        }
        for (int i = 0; i < 8; i++) {
            int n = root + (driving && (i % 2) ? 12 : 0);
            // The last eighth walks toward the next root.
            if (i == 7 && next_root != root) n = root + (next_root > root ? 2 : -1);
            put(c, bar, i * 2, CH_BASS, n, SL_BASS, i % 2 ? 44 : 58, FX_NONE, 0);
        }
        break;
    }
    }
}

/* ---------- Pads and chord parts ---------- */

static void pads(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    if (!c->slot[SL_PAD]) return;
    int chord_bars = MOODS[c->mood].chord_bars;
    int start = c->bar_in_section[bar] % chord_bars == 0;
    const Chord *ch = chord_at(c, bar, 0);
    int prev[4];
    for (int v = 0; v < c->pad_voices; v++) prev[v] = bar > 0 ? c->pad_voicing[bar - 1][v] : 0;
    chord_voice(ch, c->pad_voices, 41, 68, bar > 0 ? prev : NULL, c->pad_voicing[bar]);
    if (k == SECTION_BUILD && section_last_bar(c, bar)) {
        for (int v = 0; v < c->pad_voices; v++) {
            put(c, bar, 0, CH_PAD + v, c->pad_voicing[bar][v], SL_PAD, 50, FX_NONE, 0);
            put(c, bar, 8, CH_PAD + v, NOTE_CUT, SL_PAD, 0, FX_NONE, 0);
        }
        return;
    }
    int fresh = bar == 0 || c->section_of[bar] != c->section_of[bar - 1];
    if (!start && !fresh) return;
    int vol = 38 + (int)(c->energy_of[bar] * 18);
    for (int v = 0; v < c->pad_voices; v++) put(c, bar, 0, CH_PAD + v, c->pad_voicing[bar][v], SL_PAD, vol, FX_NONE, 0);
}

// The classic chip chord: one channel cycling root, third and fifth every tick with the 0xy effect.
static void chip_chords(Composer *c, int bar) {
    if (c->kind_of[bar] == SECTION_INTRO && c->bar_in_section[bar] < 4) {
        put_off(c, bar, 0, CH_ARP);
        return;
    }
    const Chord *ch = chord_at(c, bar, 0);
    int root = 1 + 48 + ch->root_pc;
    int third = ch->tones[1], fifth = ch->tones[2];
    int param = (third & 15) << 4 | (fifth & 15);
    static const char *rhythms[3] = {"x..x..x...x..x..", "x.x.x.x.x.x.x.x.", "x...x...x...x..."};
    const char *r = rhythms[c->kind_of[bar] == SECTION_DROP ? 0 : c->kind_of[bar] == SECTION_BREAK ? 2 : 1];
    for (int row = 0; row < 16; row++)
        if (r[row] == 'x') put(c, bar, row, CH_ARP, root, SL_ARP, 30, FX_ARPEGGIO, param);
}

static void arpeggio(Composer *c, int bar, const char *pattern, int rate, int octave_shift, int vol) {
    if (!c->slot[SL_ARP]) return;
    const Chord *ch = chord_at(c, bar, 0);
    int tones[8], n = 0;
    int base = 1 + 48 + ch->root_pc + octave_shift * 12;
    for (int i = 0; i < ch->count && n < 8; i++) tones[n++] = base + ch->tones[i];
    if (n < 4) tones[n++] = base + 12;
    int len = (int)strlen(pattern);
    for (int i = 0, row = 0; row < c->rows_per_bar; row += rate, i++) {
        int idx = pattern[i % len] - '0';
        int note = tones[idx % n] + 12 * (idx / n);
        put(c, bar, row, CH_ARP, note, SL_ARP, vol - ((row / rate) % 2) * 6, FX_NONE, 0);
    }
}

/* ---------- Melody ---------- */

typedef struct {
    int row, len, note;
} MelodyNote;

typedef struct {
    MelodyNote notes[64];
    int count;
} Melody;

static const char *const RHYTHMS_POP[] = {"x..x..x.x...x.x.", "x.x.x...x..x..x.", "x...x.x...x.x...", "..x.x.x.x...x...",
                                         "x..x..x...x.x...", "x.x...x.x.x...x."};
static const char *const RHYTHMS_CHIP[] = {"x.xxx.x.x.xxx.x.", "xx.x.x.xx.x.x.x.", "x.x.xx.xx.x.x.x.", "x.x.x.xxx.x.x.x."};
static const char *const RHYTHMS_SPARSE[] = {"x.......x...x...", "x...........x...", "x.....x...x.....", "x.......x.......",
                                            "x...x.......x..."};

static void rhythm_onsets(const char *r, int bar_offset, int rows, MelodyNote *out, int *count) {
    for (int i = 0; i < rows; i++) {
        if (r[i] != 'x') continue;
        int len = 1;
        while (i + len < rows && r[i + len] != 'x') len++;
        out[*count].row = bar_offset + i;
        out[*count].len = len;
        out[*count].note = 0;
        (*count)++;
    }
}

static int clamp_note(Composer *c, int note, int low, int high) {
    while (note < low) note = key_step(c->key, note, 7);
    while (note > high) note = key_step(c->key, note, -7);
    return note;
}

// The chord tone that best follows the phrase's contour without leaping too far from the last one.
static int skeleton_note(Composer *c, const Chord *ch, int previous, float target, int low, int high) {
    int best = previous, best_score = 1 << 30;
    for (int n = low; n <= high; n++) {
        if (!chord_has_pc(ch, n - 1)) continue;
        float leap = (float)abs(n - previous);
        // Follow the contour, avoid big leaps, and keep moving: a hook that sits on one note isn't a hook.
        float score = mabs((float)n - target) * 1.5f + (leap > 7 ? leap * 6 : leap * 0.8f) + (n == previous ? 5 : 0) +
                      rng_float(&c->rng) * 3;
        if ((int)score < best_score) {
            best_score = (int)score;
            best = n;
        }
    }
    return best;
}

// Fills the notes between two skeleton tones: passing tones when there's room to walk, neighbors otherwise.
static int fill_note(Composer *c, int from, int to, int index, int count) {
    int steps = 0, probe = from;
    while (probe != to && abs(steps) < 8) {
        int next = key_step(c->key, probe, to > probe ? 1 : -1);
        probe = next;
        steps += to > from ? 1 : -1;
    }
    if (abs(steps) >= 2 && abs(steps) <= count + 1) return key_step(c->key, from, (to > from ? 1 : -1) * (index + 1));
    if (index % 2 == 0) return key_step(c->key, from, rng_chance(&c->rng, 60) ? 1 : -1);
    return from;
}

typedef enum { CADENCE_NONE, CADENCE_HALF, CADENCE_HOME } Cadence;

// Gives pitches to onsets spanning two bars: chord tones on the beats following an arch or a fall,
// stepwise fills between them, and an ending that rests on the fifth or comes home to the tonic.
static void realize(Composer *c, int start_bar, MelodyNote *notes, int count, int low, int high, float rise, int start_note,
                    Cadence cadence) {
    int rpb = c->rows_per_bar, span = rpb * 2;
    int strong[64], skeleton[64], n_strong = 0;
    float center = (float)(low + high) / 2, amplitude = (float)(high - low) * 0.45f;
    int previous = start_note;
    for (int i = 0; i < count; i++) {
        int row = notes[i].row % span;
        if (row % 4 == 0 || i == 0 || i == count - 1) {
            float t = (float)row / (float)span;
            float shape;
            switch (c->contour) {
            case 1: shape = (t - 0.5f) * 1.4f; break;
            case 2: shape = mcos(t * TAU_F) * 0.6f; break;
            case 3: shape = msin(t * TAU_F) * 0.6f; break;
            default: shape = msin(t * PI_F) - 0.4f; break;
            }
            shape = rise > 0 ? shape * rise : (1 - t) * -rise - 0.5f * -rise;
            float target = center + amplitude * shape;
            int bar = start_bar + notes[i].row / rpb;
            const Chord *ch = chord_at(c, bar, notes[i].row % rpb);
            int n = skeleton_note(c, ch, previous, target, low, high);
            if (i == count - 1 && cadence != CADENCE_NONE) {
                int goal = cadence == CADENCE_HOME ? c->key.tonic : key_pitch_class(c->key, 4);
                int best = n, best_d = 99;
                for (int m = low; m <= high; m++)
                    if ((m - 1) % 12 == goal && abs(m - previous) < best_d) {
                        best_d = abs(m - previous);
                        best = m;
                    }
                n = best;
            }
            strong[n_strong] = i;
            skeleton[n_strong++] = n;
            previous = n;
        }
    }
    for (int k = 0; k < n_strong; k++) {
        notes[strong[k]].note = skeleton[k];
        if (k + 1 >= n_strong) continue;
        int gap = strong[k + 1] - strong[k] - 1;
        for (int j = 0; j < gap; j++) {
            int n = fill_note(c, skeleton[k], skeleton[k + 1], j, gap);
            notes[strong[k] + 1 + j].note = clamp_note(c, n, low - 2, high + 2);
        }
    }
}

// A two-bar motif and its answer make a phrase; the period plays the phrase twice, coming home the second time.
static void write_period(Composer *c, int start_bar, const char *const *rhythms, int rhythm_count, int low, int high,
                         Melody *out) {
    int rpb = c->rows_per_bar;
    const char *a = rhythms[rng_range(&c->rng, 0, rhythm_count)];
    const char *b = rhythms[rng_range(&c->rng, 0, rhythm_count)];
    MelodyNote motif[48], answer[48];
    int count = 0;
    rhythm_onsets(a, 0, rpb, motif, &count);
    rhythm_onsets(b, rpb, rpb, motif, &count);
    // The answer keeps the motif's rhythm but lets its last note ring to the end of the phrase.
    memcpy(answer, motif, sizeof(MelodyNote) * (size_t)count);
    answer[count - 1].len = 2 * rpb - answer[count - 1].row;
    float rise = rng_between(&c->rng, 0.7f, 1.0f);
    c->contour = rng_range(&c->rng, 0, 4);
    int start = chord_nearest(chord_at(c, start_bar, 0), (low + high) / 2);
    realize(c, start_bar, motif, count, low, high, rise, start, CADENCE_NONE);
    out->count = 0;
    for (int half = 0; half < 2; half++) {
        for (int part = 0; part < 2; part++) {
            int base_bar = start_bar + half * 4 + part * 2;
            if (base_bar >= c->bars) break;
            MelodyNote phrase[48];
            if (part == 0 && (half == 0 || c->chord_of[base_bar][0].root_pc == c->chord_of[start_bar][0].root_pc)) {
                memcpy(phrase, motif, sizeof(MelodyNote) * (size_t)count);
            } else {
                memcpy(phrase, part ? answer : motif, sizeof(MelodyNote) * (size_t)count);
                int from = out->count ? out->notes[out->count - 1].note : start;
                realize(c, base_bar, phrase, count, low, high, part ? -rng_between(&c->rng, 0.4f, 0.8f) : rise, from,
                        part ? (half ? CADENCE_HOME : CADENCE_HALF) : CADENCE_NONE);
            }
            for (int i = 0; i < count && out->count < 64; i++) {
                MelodyNote m = phrase[i];
                m.row += (base_bar - start_bar) * rpb;
                out->notes[out->count++] = m;
            }
        }
    }
}

static void play_melody(Composer *c, int start_bar, const Melody *m, int ch, Slot slot, int vol, int octave, int glide) {
    for (int i = 0; i < m->count; i++) {
        int bar = start_bar + m->notes[i].row / c->rows_per_bar, row = m->notes[i].row % c->rows_per_bar;
        int note = m->notes[i].note + octave * 12;
        int fx = FX_NONE, param = 0;
        if (glide && i > 0 && rng_chance(&c->rng, 18)) {
            fx = FX_TONE_PORTA;
            param = 0x40;
        }
        put(c, bar, row, ch, note, slot, vol + (row % 4 == 0 ? 6 : 0), fx, param);
        int end = m->notes[i].row + m->notes[i].len;
        int next = i + 1 < m->count ? m->notes[i + 1].row : end + 1;
        // Leave a little air between notes that don't run straight into the next.
        if (end < next) {
            int off_bar = start_bar + end / c->rows_per_bar, off_row = end % c->rows_per_bar;
            if (off_bar < c->bars) put_off(c, off_bar, off_row, ch);
        }
    }
}

// The echo channel repeats the lead three rows later and quieter, the way tracker musicians faked delay.
static void echo_melody(Composer *c, int start_bar, const Melody *m, int vol) {
    for (int i = 0; i < m->count; i++) {
        int r = m->notes[i].row + 3, end = r + m->notes[i].len;
        int bar = start_bar + r / c->rows_per_bar, row = r % c->rows_per_bar;
        if (bar < c->bars) put(c, bar, row, CH_ECHO, m->notes[i].note, SL_LEAD, vol, FX_NONE, 0);
        int next = i + 1 < m->count ? m->notes[i + 1].row + 3 : end + 1;
        int off_bar = start_bar + end / c->rows_per_bar;
        if (end < next && off_bar < c->bars) put_off(c, off_bar, end % c->rows_per_bar, CH_ECHO);
    }
}

static void melodies(Composer *c) {
    Melody hook = {0}, verse = {0};
    int have_hook = 0, have_verse = 0;
    for (int s = 0; s < c->info->section_count; s++) {
        const Section *sec = &c->info->sections[s];
        for (int b = sec->start_bar; b + 8 <= sec->start_bar + sec->bars; b += 8) {
            switch (sec->kind) {
            case SECTION_DROP:
                if (!have_hook) {
                    const char *const *r = c->mood == MOOD_KEYGEN ? RHYTHMS_CHIP : c->mood == MOOD_AMBIENT ? RHYTHMS_SPARSE : RHYTHMS_POP;
                    int n = c->mood == MOOD_KEYGEN ? 4 : c->mood == MOOD_AMBIENT ? 5 : 6;
                    write_period(c, b, r, n, c->mood == MOOD_KEYGEN ? 60 : 55, c->mood == MOOD_KEYGEN ? 79 : 74, &hook);
                    have_hook = 1;
                }
                if (c->mood == MOOD_AMBIENT) {
                    play_melody(c, b, &hook, CH_BELL, SL_BELL, 40, 0, 0);
                    if (c->slot[SL_LEAD] && b > sec->start_bar) play_melody(c, b, &hook, CH_LEAD, SL_LEAD, 34, -1, 0);
                } else {
                    play_melody(c, b, &hook, CH_LEAD, SL_LEAD, 46, 0, c->mood == MOOD_SYNTHWAVE);
                    if (c->mood == MOOD_KEYGEN) echo_melody(c, b, &hook, 18);
                    if (c->mood == MOOD_SYNTHWAVE && b > sec->start_bar) play_melody(c, b, &hook, CH_BELL, SL_BELL, 22, 1, 0);
                }
                break;
            case SECTION_VERSE:
                if (c->mood == MOOD_SYNTHWAVE && b == sec->start_bar) break;
                if (!have_verse) {
                    write_period(c, b, RHYTHMS_SPARSE, 5, 53, 70, &verse);
                    have_verse = 1;
                }
                if (c->mood == MOOD_AMBIENT)
                    play_melody(c, b, &verse, CH_BELL, SL_BELL, 32, 0, 0);
                else
                    play_melody(c, b, &verse, CH_LEAD, SL_LEAD, 36, 0, 0);
                break;
            case SECTION_BREAK:
                if (have_hook) play_melody(c, b, &hook, CH_BELL, SL_BELL, 30, 1, 0);
                break;
            default: break;
            }
        }
    }
}

/* ---------- Effects and automation ---------- */

static void effects(Composer *c) {
    for (int s = 0; s < c->info->section_count; s++) {
        const Section *sec = &c->info->sections[s];
        int start = sec->start_bar;
        if (sec->kind == SECTION_DROP) {
            put(c, start, 0, CH_FX, 49, SL_IMPACT, 50, FX_NONE, 0);
            put(c, start, 0, CH_PERC, DRUM_NOTE, SL_CRASH, 48, FX_NONE, 0);
        }
        if (sec->kind == SECTION_BUILD && sec->bars >= 2) {
            put(c, start + sec->bars - 2, 0, CH_FX, 49, SL_RISER, 44, FX_NONE, 0);
            put(c, start + sec->bars - 1, 0, CH_TEXTURE, 49, SL_REVERSE, 40, FX_NONE, 0);
        }
        if (sec->kind == SECTION_BREAK) put(c, start, 0, CH_FX, 49, SL_DOWN, 40, FX_NONE, 0);
        if (sec->kind == SECTION_INTRO && c->slot[SL_TEXTURE]) put(c, start, 0, CH_TEXTURE, 49, SL_TEXTURE, 40, FX_NONE, 0);
    }
    if (c->slot[SL_TEXTURE]) put_off(c, c->bars - 2, 0, CH_TEXTURE);
}

static void automation(Composer *c) {
    Song *s = c->song;
    int rows = c->bars * c->rows_per_bar;
    s->automation_rows = rows;
    s->duck_amount = malloc(sizeof(float) * (size_t)rows);
    s->master_cutoff = malloc(sizeof(float) * (size_t)rows);
    if (!s->duck_amount || !s->master_cutoff) {
        s->automation_rows = 0;
        return;
    }
    float depth = MOODS[c->mood].mix.duck_depth;
    for (int r = 0; r < rows; r++) {
        int bar = r / c->rows_per_bar;
        const Section *sec = &c->info->sections[c->section_of[bar]];
        float t = (float)(r - sec->start_bar * c->rows_per_bar) / (float)(sec->bars * c->rows_per_bar);
        float duck = depth * (sec->kind == SECTION_DROP ? 1.0f : sec->kind == SECTION_BREAK ? 0.15f : 0.6f);
        float cutoff = 1;
        switch (sec->kind) {
        case SECTION_INTRO: cutoff = 0.55f + 0.45f * t * t; break;
        case SECTION_BUILD: cutoff = 0.5f + 0.5f * t * t; break;
        case SECTION_OUTRO: cutoff = 1 - 0.35f * smoothstep(0.4f, 1.0f, t); break;
        default: break;
        }
        if (c->mood == MOOD_AMBIENT) cutoff = mmax(cutoff, 0.8f);
        s->duck_amount[r] = duck;
        s->master_cutoff[r] = cutoff;
    }
}

static void ending(Composer *c) {
    int bar = c->bars - 1;
    const Chord *ch = &c->chord_of[bar][0];
    if (c->slot[SL_BELL]) put(c, bar, 0, CH_BELL, 1 + 60 + ch->root_pc, SL_BELL, 40, FX_NONE, 0);
    if (c->slot[SL_PAD]) {
        int voicing[4];
        chord_voice(ch, c->pad_voices, 41, 68, NULL, voicing);
        for (int v = 0; v < c->pad_voices; v++) put(c, bar, 0, CH_PAD + v, voicing[v], SL_PAD, 46, FX_NONE, 0);
    }
    for (int ch_i = CH_BASS; ch_i <= CH_BELL; ch_i++) put_off(c, bar, c->rows_per_bar - 1, ch_i);
}

/* ---------- The whole piece ---------- */

static Mood pick_mood(Rng *r) {
    static const int weights[MOOD_COUNT] = {[MOOD_KEYGEN] = 33, [MOOD_AMBIENT] = 33, [MOOD_SYNTHWAVE] = 34};
    return (Mood)rng_weighted(r, weights, MOOD_COUNT);
}

static void write_parts(Composer *c) {
    for (int bar = 0; bar < c->bars; bar++) {
        switch (c->mood) {
        case MOOD_KEYGEN:
            drums_keygen(c, bar);
            chip_chords(c, bar);
            break;
        case MOOD_AMBIENT:
            drums_ambient(c, bar);
            pads(c, bar);
            if (c->kind_of[bar] != SECTION_INTRO || c->bar_in_section[bar] >= 4)
                arpeggio(c, bar, c->kind_of[bar] == SECTION_DROP ? "0123210" : "0213", 4, -1, 30);
            break;
        default:
            drums_synthwave(c, bar);
            pads(c, bar);
            if (c->kind_of[bar] == SECTION_VERSE || c->kind_of[bar] == SECTION_DROP ||
                (c->kind_of[bar] == SECTION_BUILD && !section_last_bar(c, bar)))
                arpeggio(c, bar, c->kind_of[bar] == SECTION_DROP ? "01232101" : "0123", 1, 0, 30);
            break;
        }
        bass_part(c, bar);
    }
    melodies(c);
    effects(c);
    ending(c);
}

int compose(uint64_t seed, const char *title, Mood mood, LengthPref length, Composition *out) {
    memset(out, 0, sizeof *out);
    Composer *c = calloc(1, sizeof *c);
    Song *song = song_create();
    if (!c || !song) {
        free(c);
        song_free(song);
        return -1;
    }
    rng_init(&c->rng, seed, "compose");
    if (mood == MOOD_ANY || !implemented(mood)) mood = pick_mood(&c->rng);
    const MoodSpec *spec = &MOODS[mood];
    SongInfo *info = &out->info;
    seed_normalize(title ? title : "", info->title, sizeof info->title);
    c->song = song;
    c->info = info;
    c->mood = mood;
    info->mood = mood;
    info->bpm = song->bpm = rng_range(&c->rng, spec->bpm_lo, spec->bpm_hi + 1);
    song->speed = 6;
    c->beats_per_bar = info->beats_per_bar = 4;
    c->rows_per_bar = info->rows_per_bar = 16;
    c->bars_per_pattern = 4;
    song->pattern_rows = c->rows_per_bar * c->bars_per_pattern;
    c->swing = spec->swing_hi > 0 ? rng_range(&c->rng, spec->swing_lo, spec->swing_hi + 1) : 0;
    info->bar_seconds = c->rows_per_bar * song->speed * 2.5 / song->bpm;
    c->key.tonic = rng_range(&c->rng, 0, 12);
    c->key.scale = (Scale)rng_weighted(&c->rng, spec->scales, SCALE_COUNT);
    info->key = c->key;
    c->pad_voices = mood == MOOD_AMBIENT ? 4 : 3;
    plan_form(c, length);
    info->bars = c->bars;
    if (rng_chance(&c->rng, (int)spec->lift_chance))
        for (int s = info->section_count - 1; s > 0; s--)
            if (info->sections[s].kind == SECTION_DROP) {
                info->lift = rng_chance(&c->rng, 50) ? 1 : 2;
                info->lift_bar = info->sections[s].start_bar;
                break;
            }
    for (int s = 0; s < info->section_count; s++) {
        info->sections[s].start = info->sections[s].start_bar * info->bar_seconds;
        info->sections[s].end = (info->sections[s].start_bar + info->sections[s].bars) * info->bar_seconds;
    }
    info->length = c->bars * info->bar_seconds;
    plan_harmony(c);
    make_instruments(c);
    song->mix = spec->mix;
    song->channels = SONG_CHANNELS;
    write_parts(c);
    automation(c);
    song->order_length = (c->bars + c->bars_per_pattern - 1) / c->bars_per_pattern;
    for (int i = 0; i < song->order_length; i++) song->order[i] = (uint8_t)i;
    free(c);
    out->song = song;
    return 0;
}

void composition_free(Composition *c) {
    song_free(c->song);
    c->song = NULL;
}
