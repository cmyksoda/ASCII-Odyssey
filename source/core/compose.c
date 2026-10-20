// SPDX-License-Identifier: GPL-3.0-only
#include "compose.h"

#include "mathx.h"
#include "synth.h"

#include <stdlib.h>
#include <string.h>

const char *const mood_names[MOOD_COUNT] = {"keygen", "ambient", "synthwave", "lo-fi", "trance",
                                            "8-bit",  "music box", "dub",       "dubstep",  "16-bit"};
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
    CH_TEXTURE,
    CH_SUB,
    CH_CHORD,
    CH_PERC2 = CH_CHORD + 4
};

typedef enum {
    SL_KICK,
    SL_SNARE,
    SL_CLAP,
    SL_HAT,
    SL_OHAT,
    SL_SHAKER,
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
    SL_SUB,
    SL_COUNT
} Slot;

// Groove choices are drawn once per piece, with a second pick for its back half so it develops instead of repeating.
typedef enum {
    G_KICK,
    G_KICK_CALM,
    G_SNARE,
    G_HATS,
    G_HATS_CALM,
    G_BASS,
    G_BASS_CALM,
    G_ARP,
    G_ARP_CALM,
    G_ARP_RATE,
    G_CHORDS,
    G_CHORDS_CALM,
    G_FILL,
    G_FLAG,
    G_FLAG2,
    G_COUNT
} GrooveSlot;

#define DRUM_NOTE 49
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

typedef struct {
    Rng rng;
    Song *song;
    SongInfo *info;
    Mood mood;
    Key key;
    int bars, rows_per_bar, beats_per_bar, bars_per_pattern;
    int swing, swing_eighths;
    int slot[SL_COUNT];
    SectionKind kind_of[MAX_BARS];
    float energy_of[MAX_BARS];
    int section_of[MAX_BARS], bar_in_section[MAX_BARS];
    Chord chord_of[MAX_BARS][2];
    int pad_voicing[MAX_BARS][4], pad_voices, pad_channel;
    int keys_voicing[MAX_BARS][4];
    // The melody's shape (an arch, a climb, a valley or a wave) and its character: how it weighs leaps
    // against steps, and how closely it follows the shape.
    int contour;
    float leap_cost, contour_pull;
    int groove[G_COUNT][2], half_bar;
    // Hashed for choices that mustn't move the random stream, so adding them leaves every other part as it was.
    uint32_t salt;
} Composer;

/* ---------- Writing into patterns ---------- */

static int section_bars(const Composer *c, int bar) {
    return c->info->sections[c->section_of[bar]].bars;
}

static int groove(const Composer *c, GrooveSlot s, int bar) {
    return c->groove[s][bar >= c->half_bar];
}

// Two different picks from a bank of n, one for each half of the piece.
static void pick_pair(Composer *c, GrooveSlot s, int n) {
    int a = rng_range(&c->rng, 0, n);
    c->groove[s][0] = a;
    c->groove[s][1] = n > 1 ? (a + rng_range(&c->rng, 1, n)) % n : a;
}

static PatternCell *cell_at(Composer *c, int bar, int row, int ch) {
    if (bar < 0 || bar >= c->bars || row < 0 || row >= c->rows_per_bar) return NULL;
    Pattern *p = song_pattern(c->song, bar / c->bars_per_pattern);
    return &p->cells[(bar % c->bars_per_pattern) * c->rows_per_bar + row][ch];
}

// Swing pushes off-beats late by a few ticks: sixteenths for most moods, eighths for lo-fi's lazier lope.
static int swing_ticks(const Composer *c, int row) {
    if (!c->swing) return 0;
    return (c->swing_eighths ? row % 4 == 2 : row & 1) ? c->swing : 0;
}

// vol is 1..64, or 0 for the instrument's default.
static void put(Composer *c, int bar, int row, int ch, int note, Slot slot, int vol, int fx, int param) {
    PatternCell *cell = cell_at(c, bar, row, ch);
    if (!cell || !c->slot[slot]) return;
    int pitched = note >= 1 && note <= 120;
    int tonal = ch >= CH_BASS || (slot >= SL_BASS && slot <= SL_PLUCK);
    if (pitched && tonal && c->info->lift && bar >= c->info->lift_bar) note += c->info->lift;
    int late = swing_ticks(c, row);
    if (late && pitched && fx == FX_NONE) {
        fx = FX_NOTE_DELAY;
        param = late;
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

// A section of a form. Parts leave by rank, highest first, when a piece's length can't hold them; rank 0 always stays.
typedef struct {
    SectionKind kind;
    int bars;
    float energy;
    int rank;
} Part;

// Drops run sixteen bars, back to back or across a real breakdown, so a short break never stalls the momentum.
static const Part FORM_EDM[] = {{SECTION_INTRO, 8, 0.25f, 0}, {SECTION_VERSE, 8, 0.5f, 2}, {SECTION_BUILD, 8, 0.7f, 0},
                                {SECTION_DROP, 16, 1.0f, 0},  {SECTION_DROP, 16, 1.0f, 1}, {SECTION_OUTRO, 8, 0.3f, 0}};
static const Part FORM_EDM_JOURNEY[] = {{SECTION_INTRO, 8, 0.25f, 0}, {SECTION_BUILD, 8, 0.7f, 0},
                                        {SECTION_DROP, 16, 1.0f, 0},  {SECTION_BREAK, 8, 0.35f, 1},
                                        {SECTION_BUILD, 8, 0.75f, 1}, {SECTION_DROP, 16, 1.0f, 1},
                                        {SECTION_OUTRO, 8, 0.3f, 0}};
static const Part FORM_CHIP[] = {{SECTION_INTRO, 4, 0.3f, 0}, {SECTION_VERSE, 16, 0.6f, 2},
                                 {SECTION_DROP, 16, 0.9f, 0}, {SECTION_BREAK, 8, 0.4f, 1},
                                 {SECTION_DROP, 16, 1.0f, 1}, {SECTION_OUTRO, 4, 0.3f, 0}};
static const Part FORM_CHIP_RUSH[] = {{SECTION_INTRO, 4, 0.3f, 0}, {SECTION_DROP, 16, 0.9f, 0},
                                      {SECTION_VERSE, 8, 0.6f, 2}, {SECTION_DROP, 16, 1.0f, 1},
                                      {SECTION_OUTRO, 4, 0.3f, 0}};
static const Part FORM_AMBIENT[] = {{SECTION_INTRO, 8, 0.2f, 0},  {SECTION_VERSE, 16, 0.4f, 2},
                                    {SECTION_DROP, 16, 0.6f, 0},  {SECTION_BREAK, 8, 0.3f, 1},
                                    {SECTION_DROP, 16, 0.65f, 1}, {SECTION_OUTRO, 8, 0.2f, 0}};
static const Part FORM_AMBIENT_TIDE[] = {{SECTION_INTRO, 8, 0.2f, 0},  {SECTION_DROP, 16, 0.5f, 0},
                                         {SECTION_BREAK, 8, 0.3f, 1},  {SECTION_VERSE, 8, 0.4f, 2},
                                         {SECTION_DROP, 16, 0.65f, 1}, {SECTION_OUTRO, 8, 0.2f, 0}};
static const Part FORM_LOFI[] = {{SECTION_INTRO, 4, 0.25f, 0}, {SECTION_VERSE, 16, 0.5f, 2},
                                 {SECTION_DROP, 16, 0.65f, 0}, {SECTION_BREAK, 8, 0.35f, 1},
                                 {SECTION_DROP, 16, 0.7f, 1},  {SECTION_OUTRO, 8, 0.3f, 0}};
static const Part FORM_LOFI_TAPE[] = {{SECTION_INTRO, 4, 0.25f, 0}, {SECTION_DROP, 16, 0.6f, 0},
                                      {SECTION_BREAK, 8, 0.35f, 1}, {SECTION_VERSE, 8, 0.5f, 2},
                                      {SECTION_DROP, 16, 0.7f, 1},  {SECTION_OUTRO, 4, 0.3f, 0}};
// Trance shows its hook in a breakdown before the first drop lands it.
static const Part FORM_TRANCE[] = {{SECTION_INTRO, 8, 0.35f, 0}, {SECTION_VERSE, 8, 0.55f, 2},
                                   {SECTION_BREAK, 8, 0.3f, 0},  {SECTION_BUILD, 8, 0.75f, 0},
                                   {SECTION_DROP, 16, 1.0f, 0},  {SECTION_DROP, 16, 1.0f, 1},
                                   {SECTION_OUTRO, 8, 0.35f, 0}};
static const Part FORM_TRANCE_DIRECT[] = {{SECTION_INTRO, 8, 0.35f, 0}, {SECTION_BUILD, 8, 0.7f, 0},
                                          {SECTION_DROP, 16, 1.0f, 0},  {SECTION_BREAK, 8, 0.3f, 1},
                                          {SECTION_BUILD, 8, 0.8f, 1},  {SECTION_DROP, 16, 1.0f, 1},
                                          {SECTION_OUTRO, 8, 0.35f, 0}};
// A game tune's theme and its answer, looped, with no builds: the drops are the theme at full tilt.
static const Part FORM_8BIT[] = {{SECTION_INTRO, 4, 0.3f, 0},  {SECTION_VERSE, 8, 0.6f, 2}, {SECTION_DROP, 16, 0.9f, 0},
                                 {SECTION_BREAK, 8, 0.45f, 1}, {SECTION_VERSE, 8, 0.6f, 2}, {SECTION_DROP, 16, 1.0f, 1},
                                 {SECTION_OUTRO, 4, 0.3f, 0}};
static const Part FORM_8BIT_LOOP[] = {{SECTION_INTRO, 4, 0.3f, 0}, {SECTION_DROP, 16, 0.9f, 0},
                                      {SECTION_VERSE, 8, 0.6f, 2}, {SECTION_BREAK, 8, 0.45f, 1},
                                      {SECTION_DROP, 16, 1.0f, 1}, {SECTION_OUTRO, 4, 0.3f, 0}};
// An adventure's theme: the strings and harp set out, the flute sings a verse, and the whole band takes the theme up.
static const Part FORM_16BIT[] = {{SECTION_INTRO, 4, 0.3f, 0},  {SECTION_VERSE, 8, 0.55f, 2},
                                  {SECTION_DROP, 16, 0.85f, 0}, {SECTION_BREAK, 8, 0.4f, 1},
                                  {SECTION_VERSE, 8, 0.6f, 2},  {SECTION_DROP, 16, 1.0f, 1},
                                  {SECTION_OUTRO, 4, 0.3f, 0}};
static const Part FORM_16BIT_QUEST[] = {{SECTION_INTRO, 4, 0.3f, 0},  {SECTION_VERSE, 16, 0.55f, 2},
                                        {SECTION_DROP, 16, 0.85f, 0}, {SECTION_BREAK, 8, 0.4f, 1},
                                        {SECTION_DROP, 16, 1.0f, 1},  {SECTION_OUTRO, 4, 0.3f, 0}};
static const Part FORM_WALTZ[] = {{SECTION_INTRO, 4, 0.2f, 0},  {SECTION_VERSE, 16, 0.4f, 2},
                                  {SECTION_DROP, 16, 0.55f, 0}, {SECTION_BREAK, 8, 0.3f, 1},
                                  {SECTION_DROP, 16, 0.6f, 1},  {SECTION_OUTRO, 4, 0.2f, 0}};
static const Part FORM_WALTZ_ROUND[] = {{SECTION_INTRO, 4, 0.2f, 0}, {SECTION_DROP, 16, 0.5f, 0},
                                        {SECTION_VERSE, 8, 0.4f, 2}, {SECTION_DROP, 16, 0.6f, 1},
                                        {SECTION_OUTRO, 4, 0.2f, 0}};
static const Part FORM_DUB[] = {{SECTION_INTRO, 8, 0.3f, 0},  {SECTION_VERSE, 16, 0.5f, 2},
                                {SECTION_DROP, 16, 0.75f, 0}, {SECTION_BREAK, 8, 0.35f, 1},
                                {SECTION_DROP, 16, 0.8f, 1},  {SECTION_OUTRO, 8, 0.3f, 0}};
static const Part FORM_DUBSTEP[] = {{SECTION_INTRO, 8, 0.3f, 0}, {SECTION_BUILD, 8, 0.75f, 0},
                                    {SECTION_DROP, 16, 1.0f, 0}, {SECTION_BREAK, 8, 0.35f, 1},
                                    {SECTION_BUILD, 8, 0.8f, 1}, {SECTION_DROP, 16, 1.0f, 1},
                                    {SECTION_OUTRO, 4, 0.3f, 0}};
static const Part FORM_DUBSTEP_GROOVE[] = {{SECTION_INTRO, 8, 0.3f, 0},  {SECTION_VERSE, 8, 0.5f, 2},
                                           {SECTION_BUILD, 8, 0.75f, 0}, {SECTION_DROP, 16, 1.0f, 0},
                                           {SECTION_DROP, 16, 1.0f, 1},  {SECTION_OUTRO, 8, 0.3f, 0}};
static const Part FORM_DUB_DEEP[] = {{SECTION_INTRO, 8, 0.3f, 0},  {SECTION_VERSE, 8, 0.5f, 2},
                                     {SECTION_DROP, 16, 0.75f, 0}, {SECTION_BREAK, 16, 0.35f, 1},
                                     {SECTION_DROP, 16, 0.8f, 1},  {SECTION_OUTRO, 8, 0.3f, 0}};

typedef struct {
    int bpm_lo, bpm_hi;
    int swing_lo, swing_hi, swing_eighths;
    // Beats per bar: 4 unless set (a waltz has 3).
    int meter;
    // Weights per scale.
    int scales[SCALE_COUNT];
    // Bars per chord: 1, or 2 for slow moods.
    int chord_bars;
    struct {
        const Part *parts;
        int count;
    } forms[2];
    // Candidate instruments per slot; a slot with none stays out of the piece.
    VoiceKind voices[SL_COUNT][6];
    MixSettings mix;
    float lift_chance;
} MoodSpec;

#define V(...) {__VA_ARGS__}
#define FORM(f) {(f), COUNT(f)}

static const MoodSpec MOODS[MOOD_COUNT] = {
    [MOOD_KEYGEN] = {.bpm_lo = 125,
                     .bpm_hi = 150,
                     .scales = {[SCALE_MAJOR] = 35, [SCALE_MINOR] = 45, [SCALE_DORIAN] = 20},
                     .chord_bars = 1,
                     .forms = {FORM(FORM_CHIP), FORM(FORM_CHIP_RUSH)},
                     .voices = {[SL_KICK] = V(VOICE_KICK_CHIP),
                                [SL_SNARE] = V(VOICE_SNARE_CHIP),
                                [SL_HAT] = V(VOICE_HAT_CHIP),
                                [SL_BASS] = V(VOICE_BASS_CHIP),
                                [SL_LEAD] = V(VOICE_LEAD_CHIP),
                                [SL_ARP] = V(VOICE_ARP_CHIP),
                                [SL_SUB] = V(VOICE_BASS_CHIP)},
                     .mix = {.reverb_size = 0.5f,
                             .reverb_damping = 0.5f,
                             .reverb_mix = 0.5f,
                             .delay_beats = 0.75f,
                             .delay_feedback = 0.3f,
                             .delay_mix = 0.45f,
                             .duck_depth = 0,
                             .duck_release = 0.1f,
                             .drive = 0.08f,
                             .pre_gain = 0.85f,
                             .master_gain = 0.667f},
                     .lift_chance = 25},

    [MOOD_AMBIENT] = {.bpm_lo = 62,
                      .bpm_hi = 84,
                      .scales = {[SCALE_MAJOR] = 30, [SCALE_LYDIAN] = 30, [SCALE_DORIAN] = 20, [SCALE_MINOR] = 20},
                      .chord_bars = 2,
                      .forms = {FORM(FORM_AMBIENT), FORM(FORM_AMBIENT_TIDE)},
                      .voices = {[SL_KICK] = V(VOICE_KICK_SOFT),
                                 [SL_SHAKER] = V(VOICE_SHAKER),
                                 [SL_BASS] = V(VOICE_BASS_SUB),
                                 [SL_PAD] = V(VOICE_PAD_GLASS, VOICE_PAD_WARM, VOICE_PAD_CHOIR, VOICE_PAD_FM,
                                              VOICE_PAD_STRINGS),
                                 [SL_LEAD] = V(VOICE_LEAD_SINE, VOICE_LEAD_VOX, VOICE_LEAD_FM),
                                 [SL_ARP] = V(VOICE_HARP, VOICE_PLUCK_KS, VOICE_MALLET),
                                 [SL_BELL] = V(VOICE_BELL_FM, VOICE_CELESTA, VOICE_VIBES),
                                 [SL_TEXTURE] = V(VOICE_FX_WIND)},
                      .mix = {.reverb_size = 0.86f,
                              .reverb_damping = 0.45f,
                              .reverb_mix = 1.0f,
                              .delay_beats = 1.5f,
                              .delay_feedback = 0.48f,
                              .delay_mix = 0.55f,
                              .duck_depth = 0,
                              .duck_release = 0.2f,
                              .drive = 0,
                              .pre_gain = 0.6f,
                              .master_gain = 0.987f},
                      .lift_chance = 0},

    [MOOD_SYNTHWAVE] = {.bpm_lo = 92,
                        .bpm_hi = 116,
                        .scales = {[SCALE_MINOR] = 60, [SCALE_DORIAN] = 25, [SCALE_HARMONIC_MINOR] = 15},
                        .chord_bars = 1,
                        .forms = {FORM(FORM_EDM), FORM(FORM_EDM_JOURNEY)},
                        .voices = {[SL_KICK] = V(VOICE_KICK),
                                   [SL_SNARE] = V(VOICE_SNARE_GATED),
                                   [SL_HAT] = V(VOICE_HAT_CLOSED),
                                   [SL_OHAT] = V(VOICE_HAT_OPEN),
                                   [SL_TOM] = V(VOICE_TOM),
                                   [SL_CRASH] = V(VOICE_CRASH),
                                   [SL_BASS] = V(VOICE_BASS_SAW, VOICE_BASS_FM, VOICE_BASS_SQUARE),
                                   [SL_PAD] = V(VOICE_PAD_SUPERSAW, VOICE_PAD_WARM, VOICE_PAD_STRINGS, VOICE_PAD_FM),
                                   [SL_LEAD] = V(VOICE_LEAD_SAW, VOICE_LEAD_SQUARE, VOICE_LEAD_FM, VOICE_LEAD_VOX),
                                   [SL_ARP] = V(VOICE_ARP_PLUCK, VOICE_MALLET),
                                   [SL_BELL] = V(VOICE_BELL_FM, VOICE_VIBES),
                                   [SL_RISER] = V(VOICE_FX_RISER),
                                   [SL_IMPACT] = V(VOICE_FX_IMPACT),
                                   [SL_REVERSE] = V(VOICE_FX_REVERSE_CRASH),
                                   [SL_DOWN] = V(VOICE_FX_DOWNLIFTER),
                                   [SL_SUB] = V(VOICE_BASS_SUB)},
                        .mix = {.reverb_size = 0.8f,
                                .reverb_damping = 0.42f,
                                .reverb_mix = 0.85f,
                                .delay_beats = 0.75f,
                                .delay_feedback = 0.42f,
                                .delay_mix = 0.6f,
                                .duck_depth = 0.45f,
                                .duck_release = 0.14f,
                                .drive = 0.18f,
                                .pre_gain = 0.9f,
                                .master_gain = 0.774f},
                        .lift_chance = 30},

    [MOOD_LOFI] = {.bpm_lo = 70,
                   .bpm_hi = 88,
                   .swing_lo = 2,
                   .swing_hi = 3,
                   .swing_eighths = 1,
                   .scales = {[SCALE_DORIAN] = 35, [SCALE_MAJOR] = 25, [SCALE_MINOR] = 25, [SCALE_MIXOLYDIAN] = 15},
                   .chord_bars = 1,
                   .forms = {FORM(FORM_LOFI), FORM(FORM_LOFI_TAPE)},
                   .voices = {[SL_KICK] = V(VOICE_KICK_SOFT, VOICE_KICK),
                              [SL_SNARE] = V(VOICE_SNARE_BRUSH, VOICE_SNARE),
                              [SL_HAT] = V(VOICE_HAT_CLOSED),
                              [SL_RIM] = V(VOICE_RIM),
                              [SL_SHAKER] = V(VOICE_SHAKER),
                              [SL_BASS] = V(VOICE_BASS_SUB, VOICE_BASS_FM, VOICE_BASS_SQUARE),
                              [SL_KEYS] = V(VOICE_KEYS_EPIANO),
                              [SL_LEAD] = V(VOICE_LEAD_SINE, VOICE_PLUCK_KS, VOICE_BELL_FM, VOICE_VIBES, VOICE_MALLET),
                              [SL_BELL] = V(VOICE_CELESTA, VOICE_VIBES),
                              [SL_TEXTURE] = V(VOICE_FX_CRACKLE)},
                   .mix = {.reverb_size = 0.62f,
                           .reverb_damping = 0.6f,
                           .reverb_mix = 0.6f,
                           .delay_beats = 0.75f,
                           .delay_feedback = 0.3f,
                           .delay_mix = 0.35f,
                           .duck_depth = 0.18f,
                           .duck_release = 0.16f,
                           .drive = 0.2f,
                           .pre_gain = 0.9f,
                           .master_gain = 0.813f},
                   .lift_chance = 0},

    [MOOD_TRANCE] =
        {.bpm_lo = 132,
         .bpm_hi = 140,
         .scales = {[SCALE_MINOR] = 55, [SCALE_HARMONIC_MINOR] = 15, [SCALE_MAJOR] = 20, [SCALE_DORIAN] = 10},
         .chord_bars = 2,
         .forms = {FORM(FORM_TRANCE), FORM(FORM_TRANCE_DIRECT)},
         .voices = {[SL_KICK] = V(VOICE_KICK),
                    [SL_SNARE] = V(VOICE_SNARE),
                    [SL_CLAP] = V(VOICE_CLAP),
                    [SL_HAT] = V(VOICE_HAT_CLOSED),
                    [SL_OHAT] = V(VOICE_HAT_OPEN),
                    [SL_CRASH] = V(VOICE_CRASH),
                    [SL_BASS] = V(VOICE_BASS_SAW, VOICE_BASS_FM, VOICE_BASS_ACID),
                    [SL_PAD] = V(VOICE_PAD_SUPERSAW, VOICE_PAD_STRINGS),
                    [SL_KEYS] = V(VOICE_STAB_SAW),
                    [SL_LEAD] = V(VOICE_LEAD_SAW, VOICE_LEAD_FM, VOICE_LEAD_VOX),
                    [SL_ARP] = V(VOICE_ARP_PLUCK, VOICE_MALLET),
                    [SL_BELL] = V(VOICE_BELL_FM, VOICE_PLUCK_KS, VOICE_CELESTA, VOICE_VIBES),
                    [SL_RISER] = V(VOICE_FX_RISER),
                    [SL_DOWN] = V(VOICE_FX_DOWNLIFTER),
                    [SL_IMPACT] = V(VOICE_FX_IMPACT),
                    [SL_REVERSE] = V(VOICE_FX_REVERSE_CRASH),
                    [SL_SUB] = V(VOICE_BASS_SUB)},
         .mix = {.reverb_size = 0.86f,
                 .reverb_damping = 0.35f,
                 .reverb_mix = 0.9f,
                 .delay_beats = 0.75f,
                 .delay_feedback = 0.45f,
                 .delay_mix = 0.55f,
                 .duck_depth = 0.6f,
                 .duck_release = 0.12f,
                 .drive = 0.2f,
                 .pre_gain = 0.85f,
                 .master_gain = 0.794f},
         .lift_chance = 45},
    // Four voices, as on an NES: two pulses, the triangle and the noise channel, played dry.

    [MOOD_8BIT] = {.bpm_lo = 118,
                   .bpm_hi = 150,
                   .scales = {[SCALE_MAJOR] = 45, [SCALE_MINOR] = 25, [SCALE_DORIAN] = 15, [SCALE_MIXOLYDIAN] = 15},
                   .chord_bars = 1,
                   .forms = {FORM(FORM_8BIT), FORM(FORM_8BIT_LOOP)},
                   .voices = {[SL_KICK] = V(VOICE_KICK_CHIP),
                              [SL_SNARE] = V(VOICE_SNARE_CHIP),
                              [SL_HAT] = V(VOICE_HAT_CHIP),
                              [SL_BASS] = V(VOICE_BASS_CHIP),
                              [SL_LEAD] = V(VOICE_LEAD_CHIP),
                              [SL_ARP] = V(VOICE_ARP_CHIP)},
                   .mix = {.reverb_size = 0.3f,
                           .reverb_damping = 0.6f,
                           .reverb_mix = 0.12f,
                           .delay_beats = 0.75f,
                           .delay_feedback = 0,
                           .delay_mix = 0,
                           .duck_depth = 0,
                           .duck_release = 0.1f,
                           .drive = 0,
                           .pre_gain = 0.85f,
                           .master_gain = 0.75f},
                   .lift_chance = 0},
    // The SNES's sampled band: strings and harp under a flute, slap bass and orchestra hits, all through its echo.

    [MOOD_16BIT] = {.bpm_lo = 92,
                    .bpm_hi = 136,
                    .scales = {[SCALE_MAJOR] = 35,
                               [SCALE_MINOR] = 25,
                               [SCALE_DORIAN] = 15,
                               [SCALE_LYDIAN] = 10,
                               [SCALE_MIXOLYDIAN] = 10,
                               [SCALE_HARMONIC_MINOR] = 5},
                    .chord_bars = 1,
                    .forms = {FORM(FORM_16BIT), FORM(FORM_16BIT_QUEST)},
                    .voices = {[SL_KICK] = V(VOICE_KICK),
                               [SL_SNARE] = V(VOICE_SNARE),
                               [SL_HAT] = V(VOICE_HAT_CLOSED),
                               [SL_TOM] = V(VOICE_TOM),
                               [SL_CRASH] = V(VOICE_CRASH),
                               [SL_BASS] = V(VOICE_BASS_SLAP),
                               [SL_PAD] = V(VOICE_PAD_STRINGS, VOICE_PAD_CHOIR),
                               [SL_LEAD] = V(VOICE_FLUTE),
                               [SL_ARP] = V(VOICE_HARP),
                               [SL_BELL] = V(VOICE_BELL_FM, VOICE_CELESTA),
                               [SL_KEYS] = V(VOICE_ORCH_HIT)},
                    .mix = {.reverb_size = 0.4f,
                            .reverb_damping = 0.6f,
                            .reverb_mix = 0.15f,
                            .delay_beats = 0.375f,
                            .delay_feedback = 0.45f,
                            .delay_mix = 0.4f,
                            .duck_depth = 0,
                            .duck_release = 0.1f,
                            .drive = 0.05f,
                            .pre_gain = 0.85f,
                            .master_gain = 1.05f},
                    .lift_chance = 20},

    [MOOD_MUSICBOX] =
        {.bpm_lo = 72,
         .bpm_hi = 100,
         .meter = 3,
         .scales = {[SCALE_MAJOR] = 50, [SCALE_LYDIAN] = 20, [SCALE_HARMONIC_MINOR] = 15, [SCALE_MINOR] = 15},
         .chord_bars = 1,
         .forms = {FORM(FORM_WALTZ), FORM(FORM_WALTZ_ROUND)},
         .voices = {[SL_LEAD] = V(VOICE_MUSICBOX),
                    [SL_KEYS] = V(VOICE_CELESTA, VOICE_VIBES),
                    [SL_BASS] = V(VOICE_HARP, VOICE_PLUCK_KS),
                    [SL_ARP] = V(VOICE_HARP, VOICE_MALLET),
                    [SL_PAD] = V(VOICE_PAD_GLASS, VOICE_PAD_CHOIR, VOICE_PAD_STRINGS),
                    [SL_BELL] = V(VOICE_BELL_FM, VOICE_CELESTA, VOICE_VIBES)},
         .mix = {.reverb_size = 0.88f,
                 .reverb_damping = 0.5f,
                 .reverb_mix = 1.0f,
                 .delay_beats = 1.0f,
                 .delay_feedback = 0.25f,
                 .delay_mix = 0.25f,
                 .duck_depth = 0,
                 .duck_release = 0.2f,
                 .drive = 0,
                 .pre_gain = 0.7f,
                 .master_gain = 0.933f},
         .lift_chance = 20},

    [MOOD_DUB] = {.bpm_lo = 116,
                  .bpm_hi = 124,
                  .swing_lo = 0,
                  .swing_hi = 1,
                  .scales = {[SCALE_MINOR] = 45, [SCALE_DORIAN] = 45, [SCALE_PHRYGIAN] = 10},
                  .chord_bars = 2,
                  .forms = {FORM(FORM_DUB), FORM(FORM_DUB_DEEP)},
                  .voices = {[SL_KICK] = V(VOICE_KICK, VOICE_KICK_SOFT),
                             [SL_RIM] = V(VOICE_RIM),
                             [SL_HAT] = V(VOICE_HAT_CLOSED),
                             [SL_SHAKER] = V(VOICE_SHAKER),
                             [SL_BASS] = V(VOICE_BASS_SUB, VOICE_BASS_SQUARE),
                             [SL_KEYS] = V(VOICE_STAB_DUB),
                             [SL_PAD] = V(VOICE_PAD_WARM, VOICE_PAD_CHOIR, VOICE_PAD_FM),
                             [SL_BELL] = V(VOICE_BELL_FM, VOICE_PLUCK_KS, VOICE_VIBES, VOICE_MALLET),
                             [SL_TEXTURE] = V(VOICE_FX_CRACKLE, VOICE_FX_WIND)},
                  .mix = {.reverb_size = 0.82f,
                          .reverb_damping = 0.55f,
                          .reverb_mix = 0.85f,
                          .delay_beats = 0.75f,
                          .delay_feedback = 0.62f,
                          .delay_mix = 0.75f,
                          .duck_depth = 0.3f,
                          .duck_release = 0.18f,
                          .drive = 0.1f,
                          .pre_gain = 0.85f,
                          .master_gain = 1.033f},
                  .lift_chance = 0},

    [MOOD_DUBSTEP] = {.bpm_lo = 138,
                      .bpm_hi = 142,
                      .scales = {[SCALE_MINOR] = 45, [SCALE_PHRYGIAN] = 35, [SCALE_HARMONIC_MINOR] = 20},
                      .chord_bars = 2,
                      .forms = {FORM(FORM_DUBSTEP), FORM(FORM_DUBSTEP_GROOVE)},
                      .voices = {[SL_KICK] = V(VOICE_KICK),
                                 [SL_SNARE] = V(VOICE_SNARE, VOICE_SNARE_GATED),
                                 [SL_CLAP] = V(VOICE_CLAP),
                                 [SL_HAT] = V(VOICE_HAT_CLOSED),
                                 [SL_OHAT] = V(VOICE_HAT_OPEN),
                                 [SL_CRASH] = V(VOICE_CRASH),
                                 [SL_BASS] = V(VOICE_BASS_WOBBLE),
                                 [SL_SUB] = V(VOICE_BASS_SUB),
                                 [SL_PAD] = V(VOICE_PAD_WARM, VOICE_PAD_STRINGS, VOICE_PAD_CHOIR, VOICE_PAD_FM),
                                 [SL_LEAD] = V(VOICE_LEAD_SAW, VOICE_LEAD_FM, VOICE_LEAD_VOX, VOICE_LEAD_SQUARE),
                                 [SL_BELL] = V(VOICE_BELL_FM, VOICE_PLUCK_KS, VOICE_VIBES),
                                 [SL_RISER] = V(VOICE_FX_RISER),
                                 [SL_DOWN] = V(VOICE_FX_DOWNLIFTER),
                                 [SL_IMPACT] = V(VOICE_FX_IMPACT),
                                 [SL_REVERSE] = V(VOICE_FX_REVERSE_CRASH)},
                      .mix = {.reverb_size = 0.75f,
                              .reverb_damping = 0.45f,
                              .reverb_mix = 0.8f,
                              .delay_beats = 0.75f,
                              .delay_feedback = 0.4f,
                              .delay_mix = 0.45f,
                              .duck_depth = 0.5f,
                              .duck_release = 0.14f,
                              .drive = 0.28f,
                              .pre_gain = 0.85f,
                              .master_gain = 0.708f},
                      .lift_chance = 20},
};

/* ---------- Form ---------- */

// A section's length in whole phrases: drops in eight-bar periods (sixteen when there's room), builds in fours or
// eights, verses in eights, and a break between two drops long enough to be a breakdown rather than a stumble.
static int phrase_bars(SectionKind kind, float bars, int between_drops) {
    int n;
    switch (kind) {
    case SECTION_DROP:
        n = (int)(bars / 8 + 0.5f) * 8;
        return n < 8 ? 8 : n;
    case SECTION_BUILD: return bars >= 6 ? 8 : 4;
    case SECTION_VERSE:
        n = (int)(bars / 8 + 0.5f) * 8;
        return n < 8 ? 8 : n;
    case SECTION_BREAK:
        n = (int)(bars / 4 + 0.5f) * 4;
        return n < (between_drops ? 8 : 4) ? (between_drops ? 8 : 4) : n;
    default:
        n = (int)(bars / 4 + 0.5f) * 4;
        return n < 4 ? 4 : n > 16 ? 16 : n;
    }
}

// A kept part's length once the form is scaled; drops take a set length when one is given.
static int part_bars(const Part *form, const int *keep, int count, int i, float scale, int drop_bars) {
    if (form[i].kind == SECTION_DROP && drop_bars) return drop_bars;
    int drop_before = 0, drop_after = 0;
    for (int k = 0; k < i; k++) drop_before |= keep[k] && form[k].kind == SECTION_DROP;
    for (int k = i + 1; k < count; k++) drop_after |= keep[k] && form[k].kind == SECTION_DROP;
    return phrase_bars(form[i].kind, (float)form[i].bars * scale, drop_before && drop_after);
}

static void plan_form(Composer *c, LengthPref length) {
    const MoodSpec *spec = &MOODS[c->mood];
    int variant = rng_range(&c->rng, 0, spec->forms[1].parts ? 2 : 1);
    const Part *form = spec->forms[variant].parts;
    int count = spec->forms[variant].count;
    static const float targets[3] = {65, 110, 170};
    float bar_seconds = (float)c->beats_per_bar * 60.0f / (float)c->info->bpm, budget = targets[length] / bar_seconds;
    // Each way of fitting the form is tried: whole parts leaving, highest rank first, with drops at their scaled
    // length or set to eight or sixteen bars. The plan nearest the length wins; running long, losing parts, drops
    // taking under two fifths of the piece and above all a single drop each cost extra, though a short piece may
    // settle for one.
    static const int drop_sizes[3] = {0, 8, 16};
    int top = 0, best_rank = 0, best_drop = 0, keep[MAX_SECTIONS];
    for (int i = 0; i < count; i++)
        if (form[i].rank > top) top = form[i].rank;
    float best_cost = 1e9f, best_scale = 1, lone = length == LENGTH_SHORT ? 0.25f : 1.0f;
    for (int rank = top; rank >= 0; rank--) {
        int natural = 0, drops = 0;
        for (int i = 0; i < count; i++) {
            keep[i] = form[i].rank <= rank;
            if (!keep[i]) continue;
            natural += form[i].bars;
            drops += form[i].kind == SECTION_DROP;
        }
        float scale = clampf(budget / (float)natural, 0.5f, 1.6f);
        for (int d = 0; d < 3; d++) {
            int planned = 0, dropped = 0;
            for (int i = 0; i < count; i++) {
                if (!keep[i]) continue;
                int bars = part_bars(form, keep, count, i, scale, drop_sizes[d]);
                planned += bars;
                if (form[i].kind == SECTION_DROP) dropped += bars;
            }
            float off = ((float)planned - budget) / budget, share = (float)dropped / (float)planned;
            float cost = (off > 0 ? off * 1.5f : -off) + 0.1f * (float)(top - rank) + (drops < 2 ? lone : 0.0f) +
                         mmax(0, 0.4f - share);
            if (cost < best_cost) {
                best_cost = cost;
                best_rank = rank;
                best_drop = drop_sizes[d];
                best_scale = scale;
            }
        }
    }
    for (int i = 0; i < count; i++) keep[i] = form[i].rank <= best_rank;
    SongInfo *info = c->info;
    info->section_count = 0;
    int bar = 0;
    for (int i = 0; i < count && info->section_count < MAX_SECTIONS; i++) {
        if (!keep[i]) continue;
        int bars = part_bars(form, keep, count, i, best_scale, best_drop);
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
static const int PROG_LOFI_MAJOR[][4] = {{1, 4, 0, 5}, {0, 5, 1, 4}, {3, 2, 1, 0}, {5, 1, 4, 0}, {3, 4, 2, 5}};
static const int PROG_LOFI_MINOR[][4] = {{0, 3, 0, 3}, {0, 3, 6, 2}, {3, 6, 2, 5}, {0, 5, 3, 4}, {5, 6, 0, 0}};
static const int PROG_TRANCE[][4] = {{0, 5, 2, 6}, {5, 6, 0, 0}, {0, 5, 3, 6},
                                     {5, 3, 0, 6}, {0, 3, 5, 6}, {0, 6, 5, 4}};
// Dub sits on its home chord and only leans away.
static const int PROG_DUB[][4] = {{0, 0, 3, 0}, {0, 6, 0, 6}, {0, 0, 5, 6}, {0, 3, 0, 6}, {0, 0, 0, 3}, {0, 5, 0, 5}};
static const int PROG_WALTZ_MAJOR[][4] = {{0, 3, 4, 0}, {0, 5, 3, 4}, {0, 4, 5, 3},
                                          {0, 1, 4, 0}, {0, 3, 0, 4}, {0, 5, 1, 4}};
// Phrygian's flat second is the darkest step there is, and dubstep leans on it.
static const int PROG_PHRYGIAN[][4] = {{0, 1, 0, 1}, {0, 0, 1, 0}, {0, 5, 1, 0}, {0, 6, 1, 0}, {0, 1, 6, 0}};
static const int PROG_WALTZ_MINOR[][4] = {{0, 3, 4, 0}, {0, 5, 3, 4}, {0, 3, 6, 2}, {0, 4, 0, 4}};

static int minor_family(Scale s) {
    return s == SCALE_MINOR || s == SCALE_DORIAN || s == SCALE_PHRYGIAN || s == SCALE_HARMONIC_MINOR;
}

static void pick_progression(Composer *c, int *out) {
    const int(*bank)[4];
    int n, minor = minor_family(c->key.scale);
    if (c->mood == MOOD_LOFI) {
        bank = minor ? PROG_LOFI_MINOR : PROG_LOFI_MAJOR;
        n = minor ? COUNT(PROG_LOFI_MINOR) : COUNT(PROG_LOFI_MAJOR);
    } else if (c->mood == MOOD_TRANCE && minor) {
        bank = PROG_TRANCE;
        n = COUNT(PROG_TRANCE);
    } else if (c->mood == MOOD_DUBSTEP && c->key.scale == SCALE_PHRYGIAN) {
        bank = PROG_PHRYGIAN;
        n = COUNT(PROG_PHRYGIAN);
    } else if (c->mood == MOOD_DUB) {
        bank = PROG_DUB;
        n = COUNT(PROG_DUB);
    } else if (c->mood == MOOD_MUSICBOX && c->key.scale != SCALE_LYDIAN) {
        bank = minor ? PROG_WALTZ_MINOR : PROG_WALTZ_MAJOR;
        n = minor ? COUNT(PROG_WALTZ_MINOR) : COUNT(PROG_WALTZ_MAJOR);
    } else if (c->key.scale == SCALE_LYDIAN && rng_chance(&c->rng, 60)) {
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
    case MOOD_LOFI:
        if (r < 40) return EXT_SEVENTH;
        if (r < 75) return EXT_NINTH;
        if (r < 88) return EXT_ADD9;
        return EXT_TRIAD;
    case MOOD_TRANCE:
        if (r < 12 && degree != 4) return EXT_ADD9;
        if (r < 22) return EXT_SUS2;
        return EXT_TRIAD;
    case MOOD_MUSICBOX: return r < 15 ? EXT_ADD9 : EXT_TRIAD;
    case MOOD_16BIT:
        if (r < 15) return EXT_ADD9;
        if (r < 25) return EXT_SUS2;
        if (r < 33 && degree != 4) return EXT_SEVENTH;
        return EXT_TRIAD;
    case MOOD_DUB:
        if (r < 50) return EXT_SEVENTH;
        if (r < 80) return EXT_NINTH;
        return EXT_SUS2;
    case MOOD_DUBSTEP: return r < 15 ? EXT_SUS2 : r < 25 ? EXT_ADD9 : EXT_TRIAD;
    default: return EXT_TRIAD;
    }
}

// Diminished triads sour a pop progression, so the chord a third below stands in: it shares two of the notes
// and stays in the mode. Borrowing from another mode instead put the melody's scale at odds with the chord.
static Chord consonant_chord(Key k, int degree, Extension e) {
    Chord ch = chord_build(k, degree, e);
    return ch.tones[2] != 6 ? ch : chord_build(k, (degree + 5) % 7, e);
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
        // Verses get their own progression; drops and breaks share the hook's, so it fits wherever it plays.
        int alt = c->kind_of[bar] == SECTION_VERSE;
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
            static const Extension home[MOOD_COUNT] = {[MOOD_AMBIENT] = EXT_ADD9,
                                                       [MOOD_LOFI] = EXT_NINTH, [MOOD_DUB] = EXT_SEVENTH};
            degree = 0;
            e = home[c->mood];
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

// Each piece hangs in its own room: the reverb's size and damping and the echo's timing and length vary around
// what the mood prefers.
static void vary_mix(Composer *c, MixSettings *m) {
    static const float echoes[3] = {0.5f, 0.75f, 1.0f};
    m->reverb_size = clampf(m->reverb_size + rng_between(&c->rng, -0.1f, 0.08f), 0.3f, 0.92f);
    m->reverb_damping = clampf(m->reverb_damping + rng_between(&c->rng, -0.12f, 0.12f), 0.2f, 0.8f);
    if (rng_chance(&c->rng, 50)) m->delay_beats = echoes[rng_range(&c->rng, 0, 3)];
    m->delay_feedback = clampf(m->delay_feedback + rng_between(&c->rng, -0.08f, 0.08f), 0.1f, 0.7f);
}

static void make_instruments(Composer *c) {
    const MoodSpec *spec = &MOODS[c->mood];
    Song *s = c->song;
    VoiceKind chosen[SL_COUNT] = {0};
    for (int sl = 0; sl < SL_COUNT; sl++) {
        int n = 0;
        while (n < 6 && spec->voices[sl][n] != VOICE_NONE) n++;
        if (n == 0 || s->instrument_count >= MAX_INSTRUMENTS) continue;
        VoiceKind kind = chosen[sl] = spec->voices[sl][rng_range(&c->rng, 0, n)];
        // A bass that is already a pure sub needs no second one under the drops.
        if (sl == SL_SUB && chosen[SL_BASS] == VOICE_BASS_SUB) continue;
        Rng voice_rng;
        rng_init(&voice_rng, rng_next(&c->rng), "voice");
        if (synth_instrument(&s->instruments[s->instrument_count], kind, &voice_rng, (float)s->bpm) == 0)
            c->slot[sl] = ++s->instrument_count;
    }
    // Mood touches on the baked sounds: lo-fi's keys wobble like tape, dub's rimshot rides the echoes.
    if (c->mood == MOOD_LOFI && c->slot[SL_KEYS]) {
        Instrument *keys = &s->instruments[c->slot[SL_KEYS] - 1];
        keys->vibrato_depth = 0.06f;
        keys->vibrato_rate = 0.9f;
    }
    if (c->mood == MOOD_DUB && c->slot[SL_RIM]) s->instruments[c->slot[SL_RIM] - 1].send_delay = 0.35f;
    // The drops' sub sits under everything at full weight, pumping only half as deep as the rest; dubstep's drops stand
    // on it hardest of all.
    // The SNES played its samples back soft at the top, so every sound in a 16-bit piece is filtered like one.
    if (c->mood == MOOD_16BIT)
        for (int i = 0; i < s->instrument_count; i++)
            if (!s->instruments[i].cutoff) s->instruments[i].cutoff = 0.84f;
    if (c->slot[SL_SUB]) {
        Instrument *sub = &s->instruments[c->slot[SL_SUB] - 1];
        sub->volume = c->mood == MOOD_DUBSTEP ? 1.6f : 1.0f;
        sub->ducked = 2;
        sub->send_reverb = sub->send_delay = 0;
    }
    // Dubstep's second, dirtier bass is made last and from a stream of its own, so the rest of a piece is unchanged.
    if (c->mood == MOOD_DUBSTEP && s->instrument_count < MAX_INSTRUMENTS) {
        Rng dirty_rng;
        rng_init(&dirty_rng, c->salt, "dirty bass");
        Instrument *dirty = &s->instruments[s->instrument_count];
        if (synth_instrument(dirty, VOICE_BASS_WOBBLE, &dirty_rng, (float)s->bpm) == 0) {
            c->slot[SL_KEYS] = ++s->instrument_count;
            dirty->growl = 1;
            dirty->resonance += 0.6f;
            dirty->volume *= 0.6f;
        }
    }
    // Dubstep's snare is darkened above about 4.6 kHz, so its noise doesn't hiss over drops the growl should own.
    if (c->mood == MOOD_DUBSTEP && c->slot[SL_SNARE]) s->instruments[c->slot[SL_SNARE] - 1].cutoff = 0.75f;
}

/* ---------- Drums ---------- */

// One bar of hits, one character per step: X accent, x normal, o ghost, . rest.
static void drum_line(Composer *c, int bar, int ch, Slot slot, const char *pattern, int volume) {
    for (int row = 0; pattern[row] && row < c->rows_per_bar; row++) {
        int v = pattern[row] == 'X'   ? volume
                : pattern[row] == 'x' ? volume * 3 / 4
                : pattern[row] == 'o' ? volume * 2 / 5
                                      : 0;
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

static const char *const SYNTH_KICKS[] = {"x...x...x...x...", "x...x...x...x.x.", "x...x...x..xx...",
                                          "x..x..x.x...x..."};
static const char *const SYNTH_KICKS_CALM[] = {"x.......x.......", "x.........x.....", "x.......x..x....",
                                               "x.....x...x....."};
static const char *const SYNTH_SNARES[] = {"....x.......x...", "....x.......x..o", "....x..o....x...",
                                           "....x.......xo.."};
static const char *const SYNTH_HATS[] = {"x.X.x.X.x.X.x.X.", "xxXxxxXxxxXxxxXx", "..X...X...X...X.",
                                         "x.XxX.X.x.XxX.X."};
static const char *const SYNTH_HATS_CALM[] = {"x.x.x.x.x.x.x.x.", "x...x...x...x...", "..x...x...x...x.",
                                              "x.xxx.x.x.xxx.x."};

static void drums_synthwave(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar];
    int last = section_last_bar(c, bar), phrase_end = (pos % 8) == 7;
    switch (k) {
    case SECTION_INTRO:
        if (pos >= 4) drum_line(c, bar, CH_HAT, SL_HAT, SYNTH_HATS_CALM[groove(c, G_HATS_CALM, bar)], 26);
        if (last) tom_fill(c, bar);
        break;
    case SECTION_VERSE:
    case SECTION_DROP:
    case SECTION_OUTRO: {
        int full = k == SECTION_DROP;
        const char *kick = full ? SYNTH_KICKS[groove(c, G_KICK, bar)] : SYNTH_KICKS_CALM[groove(c, G_KICK_CALM, bar)];
        const char *hats = full ? SYNTH_HATS[groove(c, G_HATS, bar)] : SYNTH_HATS_CALM[groove(c, G_HATS_CALM, bar)];
        drum_line(c, bar, CH_KICK, SL_KICK, kick, 60);
        drum_line(c, bar, CH_SNARE, SL_SNARE,
                  phrase_end && full ? "....x.......x.xx" : SYNTH_SNARES[groove(c, G_SNARE, bar)], 54);
        drum_line(c, bar, CH_HAT, SL_HAT, hats, 36);
        // The back half's drops open their hats on every offbeat, on the tom channel so the crash rings on.
        if (full && bar >= c->half_bar)
            drum_line(c, bar, CH_TOM, SL_OHAT, "..x...x...x...x.", 30);
        else if (full && pos % 2 == 1)
            drum_line(c, bar, CH_PERC, SL_OHAT, "..............x.", 40);
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

static const char *const CHIP_KICKS[] = {"x.....x.x.....x.", "x...x...x...x...", "x.x...x.x.x...x.",
                                         "x.....x...x.x..."};
static const char *const CHIP_KICKS_CALM[] = {"x.......x.......", "x.....x.x.......", "x.......x.x....."};
static const char *const CHIP_HATS[] = {"xxXxxxXxxxXxxxXx", "x.Xxx.Xxx.Xxx.Xx", "XxxXxxXxXxxXxxXx"};
static const char *const CHIP_HATS_CALM[] = {"x.X.x.X.x.X.x.X.", "x.x.X.x.x.x.X.x.", "..X...X...X...X."};

static void drums_keygen(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], phrase_end = (pos % 4) == 3, last = section_last_bar(c, bar);
    switch (k) {
    case SECTION_INTRO:
        drum_line(c, bar, CH_HAT, SL_HAT, "x.x.x.x.x.x.x.x.", 34);
        if (pos >= 4) drum_line(c, bar, CH_KICK, SL_KICK, CHIP_KICKS_CALM[groove(c, G_KICK_CALM, bar)], 56);
        if (last) snare_roll(c, bar, 8, 16, 1, 30, 56);
        break;
    case SECTION_VERSE:
    case SECTION_DROP: {
        int full = k == SECTION_DROP;
        drum_line(c, bar, CH_KICK, SL_KICK,
                  full ? CHIP_KICKS[groove(c, G_KICK, bar)] : CHIP_KICKS_CALM[groove(c, G_KICK_CALM, bar)], 60);
        const char *snare = phrase_end                   ? "....x.......x.xx"
                            : full && bar >= c->half_bar ? "....x..o....x.o."
                                                         : "....x.......x...";
        drum_line(c, bar, CH_SNARE, SL_SNARE, snare, 52);
        drum_line(c, bar, CH_HAT, SL_HAT,
                  full ? CHIP_HATS[groove(c, G_HATS, bar)] : CHIP_HATS_CALM[groove(c, G_HATS_CALM, bar)], 30);
        if (last && k == SECTION_VERSE) snare_roll(c, bar, 8, 16, 1, 34, 60);
        break;
    }
    case SECTION_BREAK: drum_line(c, bar, CH_HAT, SL_HAT, "x...x...x...x...", 28); break;
    case SECTION_OUTRO:
        drum_line(c, bar, CH_KICK, SL_KICK, "x.......x.......", 52);
        drum_line(c, bar, CH_HAT, SL_HAT, "x.x.x.x.x.x.x.x.", 26);
        break;
    default: break;
    }
}

static const char *const AMBIENT_KICKS[] = {"x...............", "x.......x.......", "x.........x....."};
static const char *const AMBIENT_SHAKERS[] = {"..x...x...x...x.", "x.o.x.o.x.o.x.o.", "......x.......x.",
                                              "..x.......x.o..."};

static void drums_ambient(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    if (k == SECTION_DROP) {
        drum_line(c, bar, CH_KICK, SL_KICK, AMBIENT_KICKS[groove(c, G_KICK, bar)], 44);
        drum_line(c, bar, CH_PERC, SL_SHAKER, AMBIENT_SHAKERS[groove(c, G_HATS, bar)], 26);
    } else if (k == SECTION_VERSE && c->bar_in_section[bar] >= 8) {
        drum_line(c, bar, CH_PERC, SL_SHAKER, "......x.......x.", 18);
    }
}

static const char *const LOFI_KICKS[] = {"x.........x.....", "x......x..x.....", "x.........x..x..", "x.....x...x.....",
                                         "x..x......x....."};
static const char *const LOFI_SNARES[] = {"....x.......x...", "....x..o....x...", "....x.......x..o",
                                          "....x.....o.x..."};
static const char *const LOFI_HATS[] = {"x.o.x.o.x.o.x.o.", "x.oox.o.x.oox.o.", "x.o.x.oox.o.x.oo", "o.x.o.x.o.x.o.x."};

static void drums_lofi(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], len = section_bars(c, bar);
    const char *hats = LOFI_HATS[groove(c, G_HATS, bar)];
    if (k == SECTION_INTRO || (k == SECTION_OUTRO && pos >= len / 2)) {
        if (pos >= len / 2) drum_line(c, bar, CH_HAT, SL_HAT, hats, 20);
        return;
    }
    if (k == SECTION_BREAK) {
        drum_line(c, bar, CH_PERC, SL_RIM, "....x.......x...", 28);
        drum_line(c, bar, CH_HAT, SL_HAT, "x...x...x...x...", 18);
        return;
    }
    // Every fourth bar swaps in a second kick pattern, the way a beatmaker varies a loop.
    int kick = groove(c, G_KICK, bar);
    if (pos % 4 == 3) kick = (kick + 1 + groove(c, G_FILL, bar)) % COUNT(LOFI_KICKS);
    drum_line(c, bar, CH_KICK, SL_KICK, LOFI_KICKS[kick], 56);
    drum_line(c, bar, CH_SNARE, SL_SNARE, LOFI_SNARES[groove(c, G_SNARE, bar)], 46);
    drum_line(c, bar, CH_HAT, SL_HAT, hats, 30);
    if (k == SECTION_DROP) drum_line(c, bar, CH_PERC, SL_SHAKER, "..x...x...x...x.", 16);
    if (k == SECTION_DROP && bar >= c->half_bar) drum_line(c, bar, CH_TOM, SL_RIM, "..........x.....", 22);
}

static void drums_trance(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], len = section_bars(c, bar);
    if (k == SECTION_BUILD) {
        drums_build(c, bar);
        return;
    }
    if (k == SECTION_BREAK) return;
    int intro = k == SECTION_INTRO, outro = k == SECTION_OUTRO, full = k == SECTION_DROP;
    if (!(outro && pos >= len - 2)) drum_line(c, bar, CH_KICK, SL_KICK, "x...x...x...x...", 62);
    if (!(intro && pos < 4) && !(outro && pos >= len - 4)) {
        drum_line(c, bar, CH_PERC, SL_OHAT, "..x...x...x...x.", full ? 40 : 32);
        drum_line(c, bar, CH_HAT, SL_HAT, full ? "xoxoxoxoxoxoxoxo" : "x.o.x.o.x.o.x.o.", 26);
    }
    if (!intro || pos >= len / 2) drum_line(c, bar, CH_SNARE, SL_CLAP, "....x.......x...", 48);
    if (full && bar >= c->half_bar) drum_line(c, bar, CH_SNARE, SL_SNARE, "......o.......o.", 30);
    // A short roll closes every eight bars, pulling the ear into the next phrase.
    if (!intro && !outro && pos % 8 == 7 && !section_last_bar(c, bar)) snare_roll(c, bar, 12, 16, 1, 28, 46);
}

// The noise channel's bars: s the long-noise snare, h a short-noise tick, H an accented one.
static const char *const NOISE_DRIVE[] = {"h.h.s.h.h.h.s.h.", "hhHhshHhhhHhshHh", "h.hhs.h.h.hhs.hh",
                                          "H.h.s.h.H.h.s.hs"};
static const char *const NOISE_CALM[] = {"h...s...h...s...", "h.h.s.h.h.h.s.h.", "....s.......s..."};

static void noise_line(Composer *c, int bar, const char *p, int vol) {
    for (int row = 0; p[row] && row < c->rows_per_bar; row++) {
        if (p[row] == 's') drum(c, bar, row, CH_SNARE, SL_SNARE, vol);
        if (p[row] == 'h' || p[row] == 'H') drum(c, bar, row, CH_SNARE, SL_HAT, p[row] == 'H' ? vol * 3 / 4 : vol / 2);
    }
}

// An NES has a single noise channel, so the snare and the hats take turns on it; the kick is the triangle's.
static void drums_8bit(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], last = section_last_bar(c, bar);
    switch (k) {
    case SECTION_INTRO:
        if (pos >= 2) noise_line(c, bar, last ? "h.h.s.h.s.s.ssss" : "h.h.h.h.h.h.h.h.", 40);
        break;
    case SECTION_VERSE:
    case SECTION_DROP: {
        int full = k == SECTION_DROP;
        const char *p = full ? NOISE_DRIVE[groove(c, G_HATS, bar)] : NOISE_CALM[groove(c, G_HATS_CALM, bar)];
        // Every phrase ends on a roll, and a section's last bar rolls harder into whatever comes next.
        if (last)
            p = "h.h.s.h.s.ssssss";
        else if (pos % 4 == 3)
            p = "h.h.s.h.h.h.s.ss";
        noise_line(c, bar, p, full ? 52 : 44);
        break;
    }
    case SECTION_BREAK: noise_line(c, bar, last ? "........s.s.ssss" : "h.......s.......", 36); break;
    case SECTION_OUTRO: noise_line(c, bar, "h.h.s.h.h.h.s.h.", 36); break;
    default: break;
    }
}

static const char *const MARCH_KICKS[] = {"x.......x.......", "x.....x.x.......", "x...x...x...x...",
                                          "x.....x...x.x..."};
static const char *const MARCH_HATS[] = {"x.x.x.x.x.x.x.x.", "x.xxx.x.x.xxx.x.", "..x...x...x...x."};

// An adventure's kit: a march under the theme, quiet hats elsewhere, and timpani rolling into each new section.
static void drums_16bit(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], last = section_last_bar(c, bar);
    switch (k) {
    case SECTION_VERSE:
    case SECTION_DROP: {
        int full = k == SECTION_DROP;
        drum_line(c, bar, CH_KICK, SL_KICK, full ? MARCH_KICKS[groove(c, G_KICK, bar)] : "x.......x.......",
                  full ? 56 : 48);
        drum_line(c, bar, CH_SNARE, SL_SNARE, full && pos % 4 == 3 ? "....x.......x.xx" : "....x.......x...",
                  full ? 50 : 40);
        drum_line(c, bar, CH_HAT, SL_HAT, MARCH_HATS[groove(c, G_HATS, bar)], full ? 28 : 22);
        if (last) tom_fill(c, bar);
        break;
    }
    case SECTION_INTRO:
    case SECTION_BREAK:
        if (last) tom_fill(c, bar);
        break;
    case SECTION_OUTRO:
        if (pos == 0) drum_line(c, bar, CH_KICK, SL_KICK, "x...............", 46);
        break;
    default: break;
    }
}

static const char *const STEP_KICKS[] = {"x.........x.....", "x.......x.x.....", "x.....x...x.....",
                                         "x.........x..x.."};
static const char *const STEP_HATS[] = {"x.x.x.x.x.x.x.x.", "x..x..x..x..x.x.", "xxx.x.xxx.x.x.x.", "..x...x...x...x."};

static void drums_dubstep(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], len = section_bars(c, bar);
    switch (k) {
    case SECTION_INTRO:
        if (pos >= len / 2) drum_line(c, bar, CH_HAT, SL_HAT, STEP_HATS[groove(c, G_HATS_CALM, bar)], 20);
        break;
    case SECTION_VERSE:
    case SECTION_DROP: {
        int full = k == SECTION_DROP;
        drum_line(c, bar, CH_KICK, SL_KICK, STEP_KICKS[groove(c, G_KICK, bar)], 62);
        drum_line(c, bar, CH_SNARE, SL_SNARE, pos % 4 == 3 ? "........X.....oX" : "........X.......", 60);
        drum_line(c, bar, CH_PERC, SL_CLAP, "........x.......", 42);
        drum_line(c, bar, CH_HAT, SL_HAT, STEP_HATS[groove(c, G_HATS, bar)], 22);
        if (full && bar >= c->half_bar) drum_line(c, bar, CH_PERC2, SL_OHAT, "......x.......x.", 20);
        break;
    }
    case SECTION_BUILD: drums_build(c, bar); break;
    case SECTION_BREAK:
        if (pos >= len / 2) drum_line(c, bar, CH_HAT, SL_HAT, "x...x...x...x...", 18);
        break;
    case SECTION_OUTRO:
        if (pos < len / 2) {
            drum_line(c, bar, CH_KICK, SL_KICK, "x.........x.....", 52);
            drum_line(c, bar, CH_SNARE, SL_SNARE, "........X.......", 50);
        }
        break;
    default: break;
    }
}

static const char *const DUB_RIMS[] = {"....x.......x...", "......x.......x.", "...x......x.....", "....x.....x...x."};

static void drums_dub(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], len = section_bars(c, bar);
    if (k == SECTION_BREAK) {
        if (pos >= len / 2) drum_line(c, bar, CH_HAT, SL_HAT, "..x...x...x...x.", 18);
        return;
    }
    int intro = k == SECTION_INTRO, outro = k == SECTION_OUTRO;
    if (!(outro && pos >= len - 4)) drum_line(c, bar, CH_KICK, SL_KICK, "x...x...x...x...", 58);
    if (!(intro && pos < 4) && !(outro && pos >= len - 2))
        drum_line(c, bar, CH_HAT, SL_HAT, "..x...x...x...x.", k == SECTION_DROP ? 30 : 24);
    if (k == SECTION_VERSE || k == SECTION_DROP || (intro && pos >= len / 2))
        drum_line(c, bar, CH_SNARE, SL_RIM, DUB_RIMS[groove(c, G_SNARE, bar)], 32);
    if (k == SECTION_DROP) drum_line(c, bar, CH_PERC, SL_SHAKER, "oxoxoxoxoxoxoxox", 18);
}

/* ---------- Bass ---------- */

static int bass_note(const Chord *ch) {
    // Roots live between E1 and D#2.
    int n = 1 + 12 + ch->root_pc;
    if (n < 17) n += 12;
    return n;
}

// Wobble bass a bar at a time: notes (r root, o octave, f fifth, m t u the scale's second, third and fourth, b the
// step below, x a cut, v V ^ glides, - a glide going on) and the wobble's rate for each beat (h half notes, q quarters,
// e eighths, t eighth triplets, s sixteenths, - still).
typedef struct {
    const char *notes, *rates;
} Wobble;

static const Wobble WOBBLES[] = {{"r.......r.......", "qqee"}, {"r...r...r...o...", "eets"},
                                 {"r.......f...r...", "tttq"}, {"r.r.r.r.o...r...", "ssqe"},
                                 {"r.......x.r.....", "hhes"}, {"r...m...r...o...", "eeee"}};
static const Wobble WOBBLE_FILLS[] = {
    {"r.r.r.r.r.r.r.r.", "ssss"}, {"o...r...o...r...", "ttss"}, {"r.......o.r.o.r.", "qqss"}};
// More bars for the phrases after the first: long growls that speed up, dotted and triplet rhythms, stutters, and
// held notes that stop wobbling to breathe.
static const Wobble WOBBLES_MORE[] = {
    {"r...............", "hhqq"}, {"r.......r.r.....", "eeqs"}, {"r..r..r..r..r...", "tttt"},
    {"r.....r.....r...", "qeqe"}, {"o.......r.......", "sseq"}, {"r...x...r...x...", "eeee"},
    {"r.r.....o.o.....", "sstt"}, {"r...........f...", "qqqs"}, {"r.......m...r...", "teqe"},
    {"r.r.r.r.r.......", "s-eq"}, {"r...r...........", "ee--"}, {"o...o...r...r...", "qqss"}};
// Phrase endings: rolls, a dead stop for half a bar, and a run that speeds up into the next downbeat.
static const Wobble FILLS_MORE[] = {{"r.......x.......", "ss--"}, {"rrrrrrrrrrrrrrrr", "eest"},
                                    {"o.o.o.o.r.r.r.r.", "ssss"}, {"r.r.r.r.x...o...", "tt-s"},
                                    {"r.r.r.r.o...V---", "ssq-"}, {"o.o.o.o.v...^---", "sss-"}};
// Riffs that move: climbing and falling through the scale, jumping octaves, and diving home.
static const Wobble RIFFS[] = {{"r...t...u...o...", "eeqq"}, {"o...u...t...r...", "qqee"}, {"r.r.b...r.r.u...", "sses"},
                               {"o.......V---r...", "qq-e"}, {"r...^---o...v...", "e-qs"}, {"r.t.u.t.r...o...", "ssqe"},
                               {"r...b...r...f...", "tete"}, {"o.o.v...o.o.v...", "ssss"}};

static int wobble_rate(char code) {
    switch (code) {
    case 'h': return 2;
    case 'q': return 4;
    case 'e': return 8;
    case 't': return 12;
    case 's': return 16;
    default: return 0;
    }
}

static void wobble_bar(Composer *c, int bar, const Wobble *w, Slot slot, int vol) {
    const Chord *ch = chord_at(c, bar, 0);
    int root = bass_note(ch) + 12;
    for (int row = 0; row < c->rows_per_bar; row++) {
        int rate = wobble_rate(w->rates[row / 4]), n = -1, fx = FX_WOBBLE, param = rate;
        // Glides land exactly on their note: a quick dive to the root within the row, or a slow one, or a rise to
        // the octave, over the beat the following rows carry on.
        switch (w->notes[row]) {
        case 'r': n = root; break;
        case 'o': n = root + 12; break;
        case 'f': n = root + 7; break;
        case 'm': n = key_step(c->key, root, 1); break;
        case 't': n = key_step(c->key, root, 2); break;
        case 'u': n = key_step(c->key, root, 3); break;
        case 'b': n = key_step(c->key, root, -1); break;
        case 'v': n = root, fx = FX_TONE_PORTA, param = 0x27; break;
        case 'V': n = root, fx = FX_TONE_PORTA, param = 0x0a; break;
        case '^': n = root + 12, fx = FX_TONE_PORTA, param = 0x0a; break;
        case '-': {
            PatternCell *cell = cell_at(c, bar, row, CH_BASS);
            if (cell && !cell->note) cell->effect = FX_TONE_PORTA, cell->param = 0;
            continue;
        }
        case 'x': put(c, bar, row, CH_BASS, NOTE_CUT, slot, 0, FX_NONE, 0); continue;
        default: break;
        }
        if (n >= 0) {
            put(c, bar, row, CH_BASS, n, slot, vol, fx, param);
        } else if (row % 4 == 0) {
            // A held note changes its wobble on the beat.
            PatternCell *cell = cell_at(c, bar, row, CH_BASS);
            if (cell && !cell->note) {
                cell->effect = FX_WOBBLE;
                cell->param = (uint8_t)rate;
            }
        }
    }
}

static void wobble_bass(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], len = section_bars(c, bar);
    switch (k) {
    case SECTION_DROP:
    case SECTION_VERSE: {
        // A section's first phrase states the piece's own groove; every phrase after it deals new bars, so the
        // wobble keeps changing instead of going round the same two.
        int phrase = pos / 4, slot = pos % 4;
        const Wobble *w;
        if (slot == 3) {
            uint32_t h = hash_u32(c->salt + (uint32_t)(c->section_of[bar] * 131 + phrase) * 7919u);
            w = phrase && h % 3 ? &FILLS_MORE[(h / 3) % COUNT(FILLS_MORE)] : &WOBBLE_FILLS[groove(c, G_FILL, bar)];
        } else if (!phrase) {
            w = &WOBBLES[groove(c, slot % 2 ? G_BASS_CALM : G_BASS, bar)];
        } else {
            uint32_t h = hash_u32(c->salt + (uint32_t)(c->section_of[bar] * 131 + phrase * 3 + slot % 2) * 104729u);
            w = h % 2 ? &RIFFS[(h / 2) % COUNT(RIFFS)] : &WOBBLES_MORE[(h / 2) % COUNT(WOBBLES_MORE)];
        }
        // Every other phrase of a drop hands the riff to a second, dirtier bass, the way brostep trades growls.
        Slot voice = k == SECTION_DROP && phrase % 2 && c->slot[SL_KEYS] ? SL_KEYS : SL_BASS;
        wobble_bar(c, bar, w, voice, k == SECTION_DROP ? 58 : 46);
        break;
    }
    case SECTION_BUILD: {
        // The wobble speeds up through the build, then stops dead half way through its last bar.
        static const Wobble climb[4] = {{"r...............", "qqqq"}, {"r...............", "eeee"},
                                        {"r.......r.......", "ssss"}, {"r.......x.......", "ss--"}};
        int stage = section_last_bar(c, bar) ? 3 : pos * 3 / len;
        wobble_bar(c, bar, &climb[stage], SL_BASS, 44 + stage * 4);
        break;
    }
    case SECTION_INTRO:
        if (pos >= len / 2) {
            static const Wobble tease = {"r...............", "hhhh"};
            wobble_bar(c, bar, &tease, SL_BASS, 34);
        }
        break;
    default:
        if (bar > 0 && c->kind_of[bar - 1] != k) put_off(c, bar, 0, CH_BASS);
        break;
    }
}

static int bass_rests(const Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], half = section_bars(c, bar) / 2;
    switch (c->mood) {
    case MOOD_AMBIENT: return 0;
    case MOOD_LOFI:
    case MOOD_TRANCE: return k == SECTION_INTRO || k == SECTION_BREAK;
    case MOOD_8BIT: return k == SECTION_INTRO && pos < 2;
    case MOOD_DUB: return (k == SECTION_INTRO && pos < half) || k == SECTION_BREAK;
    default: return (k == SECTION_INTRO && pos < 4) || k == SECTION_BREAK;
    }
}

static const char *const SYNTH_BASS[] = {"r.o.r.o.r.o.r.oa", "rr.rrr.rrr.rrr.a", "r.r.o.r.r.r.o.ra", "rrrrrrrrrrrrrrra",
                                         "r..r..r.o..r..ra"};
static const char *const SYNTH_BASS_CALM[] = {"r.r.r.r.r.r.r.ra", "r.......r.....a.", "r..r..r...r..r.a",
                                              "r...r...r...r..a"};
static const char *const CHIP_BASS[] = {"r.o.r.o.f.o.r.o.", "r.r.o.r.r.r.o.r.", "r..r..o.r..r..f.", "r.f.o.f.r.f.o.f."};
static const char *const SLAP_DRIVE[] = {"r..r.o.r..r.o.r.", "r.o.r..or.o.r..o", "r..ro..rr..ro..r",
                                         "r.r.o.r.f.r.o.ra"};
static const char *const SLAP_CALM[] = {"r.......r...f...", "r.....r.r.....f.", "r...o...r...o..."};
static const char *const LOFI_BASS[] = {"r.........f.....", "r.....f...o.....", "r.......f.....c.", "r.........f...c."};
static const char *const DUB_BASS[] = {"r.........f.....", "r.....f.........", "r......f..o.....", "r.........f...o."};

// One bar of bass from a pattern: r the root, o its octave, f its fifth, a a scale step toward the next chord's
// root, c a chromatic step up into it.
static void bass_pattern(Composer *c, int bar, const char *p, int octave) {
    const Chord *ch = chord_at(c, bar, 0);
    int root = bass_note(ch) + octave * 12;
    int next = bar + 1 < c->bars ? bass_note(&c->chord_of[bar + 1][0]) + octave * 12 : root;
    for (int row = 0; p[row] && row < c->rows_per_bar; row++) {
        int n = root;
        switch (p[row]) {
        case 'r': break;
        case 'o': n = root + 12; break;
        case 'f': n = root + ch->tones[2]; break;
        case 'a':
            if (next != root) n = key_step(c->key, root, next > root ? 1 : -1);
            break;
        case 'c':
            if (next != root) n = next - 1;
            break;
        default: continue;
        }
        // Held back outside the drops, so a drop arrives with more weight than anything before it.
        int vol = (row % 4 == 0 ? 58 : 46) * (c->kind_of[bar] == SECTION_DROP ? 100 : 82) / 100;
        put(c, bar, row, CH_BASS, n, SL_BASS, vol, FX_NONE, 0);
    }
}

// The triangle's bars: bass as in bass_pattern, plus k, the triangle's own pitch-swept kick, cutting into the bass
// on the beat the way NES drivers play it.
static const char *const TRIANGLE_DRIVE[] = {"k.r.o.r.k.r.o.r.", "k.o.r.o.k.o.f.o.", "k.r.r.o.k.r.o.ra",
                                             "k.o.f.o.k.r.o.a."};
static const char *const TRIANGLE_CALM[] = {"k...r...k...r...", "k.r...o.k.r...o.", "k...r.o.k...r.o."};

static void triangle(Composer *c, int bar, const char *p) {
    bass_pattern(c, bar, p, 1);
    for (int row = 0; p[row] && row < c->rows_per_bar; row++)
        if (p[row] == 'k') put(c, bar, row, CH_BASS, DRUM_NOTE, SL_KICK, 58, FX_NONE, 0);
}

// Drops stand on a sine (or a chip triangle) under the bass's own root, held for each chord and pumping with the kick.
static void sub_part(Composer *c, int bar) {
    if (!c->slot[SL_SUB]) return;
    if (c->kind_of[bar] != SECTION_DROP) {
        if (bar > 0 && c->kind_of[bar - 1] == SECTION_DROP) put_off(c, bar, 0, CH_SUB);
        return;
    }
    int fresh = bar == 0 || c->section_of[bar] != c->section_of[bar - 1];
    if (fresh || c->bar_in_section[bar] % MOODS[c->mood].chord_bars == 0)
        put(c, bar, 0, CH_SUB, bass_note(chord_at(c, bar, 0)), SL_SUB, 62, FX_NONE, 0);
}

// A drop that follows anything but a build still gets a snare run into it, so it never just appears.
static void drop_fills(Composer *c) {
    if (!c->slot[SL_SNARE] || !c->slot[SL_SUB]) return;
    for (int s = 1; s < c->info->section_count; s++) {
        const Section *sec = &c->info->sections[s];
        if (sec->kind != SECTION_DROP || sec[-1].kind == SECTION_BUILD) continue;
        snare_roll(c, sec->start_bar - 1, c->rows_per_bar / 2, c->rows_per_bar, 1, 30, 58);
    }
}

static void bass_part(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    if (c->mood == MOOD_MUSICBOX) return;
    if (bass_rests(c, bar)) {
        // A looping bass sample would otherwise drone on through the rest.
        put_off(c, bar, 0, CH_BASS);
        return;
    }
    const Chord *ch = chord_at(c, bar, 0);
    int root = bass_note(ch);
    int build_end = k == SECTION_BUILD && section_last_bar(c, bar);
    switch (c->mood) {
    case MOOD_LOFI: bass_pattern(c, bar, LOFI_BASS[groove(c, G_BASS, bar)], 0); break;
    case MOOD_DUB: bass_pattern(c, bar, DUB_BASS[groove(c, G_BASS, bar)], 0); break;
    case MOOD_DUBSTEP: wobble_bass(c, bar); break;
    case MOOD_TRANCE: {
        // Rolling sixteenths between the kicks, or one offbeat eighth per beat.
        const char *line = groove(c, G_FLAG, bar) ? ".xxx.xxx.xxx.xxx" : "..x...x...x...x.";
        for (int row = 0; row < (build_end ? 8 : c->rows_per_bar); row++)
            if (line[row] == 'x') put(c, bar, row, CH_BASS, root, SL_BASS, row % 4 == 2 ? 58 : 46, FX_NONE, 0);
        if (build_end) put(c, bar, 8, CH_BASS, NOTE_CUT, SL_BASS, 0, FX_NONE, 0);
        break;
    }
    case MOOD_8BIT:
        triangle(c, bar, k == SECTION_DROP    ? TRIANGLE_DRIVE[groove(c, G_BASS, bar)]
                         : k == SECTION_BREAK ? "r.......o......."
                                              : TRIANGLE_CALM[groove(c, G_BASS_CALM, bar)]);
        break;
    case MOOD_AMBIENT:
        if (c->bar_in_section[bar] % MOODS[c->mood].chord_bars == 0)
            put(c, bar, 0, CH_BASS, root, SL_BASS, 52, FX_NONE, 0);
        break;
    case MOOD_KEYGEN: bass_pattern(c, bar, CHIP_BASS[groove(c, G_BASS, bar)], 1); break;
    case MOOD_16BIT:
        bass_pattern(
            c, bar, k == SECTION_DROP ? SLAP_DRIVE[groove(c, G_BASS, bar)] : SLAP_CALM[groove(c, G_BASS_CALM, bar)], 0);
        break;
    default:
        if (build_end) {
            put(c, bar, 0, CH_BASS, root, SL_BASS, 56, FX_NONE, 0);
            put(c, bar, 8, CH_BASS, NOTE_CUT, SL_BASS, 0, FX_NONE, 0);
        } else if (k == SECTION_DROP || k == SECTION_BUILD) {
            bass_pattern(c, bar, SYNTH_BASS[groove(c, G_BASS, bar)], 0);
        } else {
            bass_pattern(c, bar, SYNTH_BASS_CALM[groove(c, G_BASS_CALM, bar)], 0);
        }
        break;
    }
}

/* ---------- Pads and chord parts ---------- */

// Voices the bar's chord close to where the same part's voices were a bar ago.
static void voice_chord(Composer *c, int bar, int voicing[][4], int voices, int low, int high) {
    const int *prev = bar > 0 && voicing[bar - 1][0] ? voicing[bar - 1] : NULL;
    chord_voice(chord_at(c, bar, 0), voices, low, high, prev, voicing[bar]);
}

static void pads(Composer *c, int bar, int channel, int percent) {
    SectionKind k = c->kind_of[bar];
    if (!c->slot[SL_PAD]) return;
    int chord_bars = MOODS[c->mood].chord_bars;
    int start = c->bar_in_section[bar] % chord_bars == 0;
    voice_chord(c, bar, c->pad_voicing, c->pad_voices, 41, 68);
    if (k == SECTION_BUILD && section_last_bar(c, bar)) {
        for (int v = 0; v < c->pad_voices; v++) {
            put(c, bar, 0, channel + v, c->pad_voicing[bar][v], SL_PAD, 50, FX_NONE, 0);
            put(c, bar, 8, channel + v, NOTE_CUT, SL_PAD, 0, FX_NONE, 0);
        }
        return;
    }
    int fresh = bar == 0 || c->section_of[bar] != c->section_of[bar - 1];
    if (!start && !fresh) return;
    int vol = (38 + (int)(c->energy_of[bar] * 18)) * percent / 100;
    for (int v = 0; v < c->pad_voices; v++)
        put(c, bar, 0, channel + v, c->pad_voicing[bar][v], SL_PAD, vol, FX_NONE, 0);
}

// Looping pads ring until told to stop, so a part that pauses lets go of its voices.
static void release_pads(Composer *c, int bar, int channel) {
    for (int v = 0; v < c->pad_voices; v++) put_off(c, bar, 0, channel + v);
}

static const char *const LOFI_COMPS[] = {"x.........x.....", "x.....x.........", "x.......x.....x.", "x..........x....",
                                         "x.....x...x....."};

static void keys_lofi(Composer *c, int bar) {
    if (!c->slot[SL_KEYS]) return;
    SectionKind k = c->kind_of[bar];
    voice_chord(c, bar, c->keys_voicing, 4, 50, 74);
    const char *comp =
        k == SECTION_VERSE || k == SECTION_DROP ? LOFI_COMPS[groove(c, G_CHORDS, bar)] : "x...............";
    for (int row = 0; row < c->rows_per_bar; row++) {
        if (comp[row] != 'x') continue;
        for (int v = 0; v < 4; v++) {
            // Rolled from the bottom up a tick apart, like a hand rather than a block; the delay must land inside the row.
            int delay = v + swing_ticks(c, row);
            if (delay > c->song->speed - 1) delay = c->song->speed - 1;
            put(c, bar, row, CH_PAD + v, c->keys_voicing[bar][v], SL_KEYS, (row ? 30 : 36) - v * 2,
                delay ? FX_NOTE_DELAY : FX_NONE, delay);
        }
    }
}

static const char *const GATES[] = {"x.xx.x.xx.x.x.xx", "xx.xx.x.xx.xx.x.", "x.x.x.xxx.x.x.xx", "x..x..x.x..x..x."};

// Trance's gated chords: supersaw stabs on a sixteenth-note rhythm, louder on the beat.
static void gates(Composer *c, int bar) {
    if (!c->slot[SL_KEYS]) return;
    voice_chord(c, bar, c->keys_voicing, 3, 49, 72);
    const char *gate = GATES[groove(c, G_CHORDS, bar)];
    for (int row = 0; row < c->rows_per_bar; row++)
        if (gate[row] == 'x')
            for (int v = 0; v < 3; v++)
                put(c, bar, row, CH_CHORD + v, c->keys_voicing[bar][v], SL_KEYS, (row % 4 == 0 ? 42 : 34) - v * 2,
                    FX_NONE, 0);
}

static const char *const DUB_STABS[] = {"..x.............", "......x.........", "..x.......x.....", "......x.....x...",
                                        "...x..........x."};

static void stabs(Composer *c, int bar) {
    if (!c->slot[SL_KEYS]) return;
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], len = section_bars(c, bar);
    if ((k == SECTION_INTRO && pos < len / 4) || (k == SECTION_OUTRO && pos >= len - 2)) return;
    // Breaks keep one stab every other bar, so the echoes have room.
    if (k == SECTION_BREAK && pos % 2) return;
    voice_chord(c, bar, c->keys_voicing, 4, 50, 70);
    const char *pattern = DUB_STABS[groove(c, pos % 4 == 3 ? G_CHORDS_CALM : G_CHORDS, bar)];
    // The filter breathes over sixteen bars, like a hand riding the cutoff knob.
    int cutoff = 90 + (int)(70 * (0.5f - 0.5f * mcos((float)bar * TAU_F / 16)));
    for (int row = 0; row < c->rows_per_bar; row++) {
        if (pattern[row] != 'x') continue;
        for (int v = 0; v < 4; v++)
            put(c, bar, row, CH_PAD + v, c->keys_voicing[bar][v], SL_KEYS, 40 - v * 2, FX_FILTER, cutoff);
        if (k == SECTION_BREAK) break;
    }
}

static const char *const PULSE_ARPS[] = {"x.x.x.x.x.x.x.x.", "x..x..x.x..x..x.", "x...x...x...x...",
                                         "x.xx.xx.x.xx.xx."};

// Pulse two carries the chords between the drops with the 0xy arpeggio, then gives the channel to the hook's echo.
static void pulse_chords(Composer *c, int bar) {
    if (c->kind_of[bar] == SECTION_DROP) {
        put_off(c, bar, 0, CH_ARP);
        return;
    }
    const Chord *ch = chord_at(c, bar, 0);
    int root = 1 + 48 + ch->root_pc, param = (ch->tones[1] & 15) << 4 | (ch->tones[2] & 15);
    const char *r = PULSE_ARPS[groove(c, G_CHORDS, bar)];
    for (int row = 0; row < c->rows_per_bar; row++)
        if (r[row] == 'x') put(c, bar, row, CH_ARP, root, SL_ARP, 28, FX_ARPEGGIO, param);
}

// The orchestra hit, on the chord's root at the top of each of a drop's phrases.
static void orch_hits(Composer *c, int bar) {
    if (!c->slot[SL_KEYS] || c->kind_of[bar] != SECTION_DROP || c->bar_in_section[bar] % 4) return;
    put(c, bar, 0, CH_CHORD, 1 + 48 + chord_at(c, bar, 0)->root_pc, SL_KEYS, 44, FX_NONE, 0);
}

// The music box's waltz: a low harp root on the downbeat, then two chord tones on beats two and three.
// The dyads borrow the drum channels, which a music box never needs.
static void waltz(Composer *c, int bar) {
    SectionKind k = c->kind_of[bar];
    int pos = c->bar_in_section[bar], len = section_bars(c, bar);
    if (k == SECTION_INTRO) return;
    const Chord *ch = chord_at(c, bar, 0);
    int root = 1 + 36 + ch->root_pc;
    if (root > 44) root -= 12;
    put(c, bar, 0, CH_BASS, root, SL_BASS, 46, FX_NONE, 0);
    if (k == SECTION_BREAK || k == SECTION_DROP || (k == SECTION_OUTRO && pos >= len - 2)) return;
    voice_chord(c, bar, c->keys_voicing, 2, 56, 70);
    for (int beat = 1; beat < 3; beat++)
        for (int v = 0; v < 2; v++)
            put(c, bar, beat * 4, CH_HAT + v, c->keys_voicing[bar][v], SL_KEYS, (beat == 1 ? 30 : 26) - v * 2, FX_NONE,
                0);
}

static const char *const CHIP_STABS[] = {"x..x..x...x..x..", "x.x..x.x.x..x.x.", "x...x.x...x.x...",
                                         "xx.x.x.xx.x.x.x."};
static const char *const CHIP_STABS_CALM[] = {"x.x.x.x.x.x.x.x.", "x..x..x.x..x..x.", "x...x...x...x..."};

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
    SectionKind k = c->kind_of[bar];
    const char *r = k == SECTION_DROP    ? CHIP_STABS[groove(c, G_CHORDS, bar)]
                    : k == SECTION_BREAK ? "x...x...x...x..."
                                         : CHIP_STABS_CALM[groove(c, G_CHORDS_CALM, bar)];
    for (int row = 0; row < 16; row++)
        if (r[row] == 'x') put(c, bar, row, CH_ARP, root, SL_ARP, 30, FX_ARPEGGIO, param);
}

// Arp orders, as indexes into the chord's tones (past the top, they climb an octave).
static const char *const ARPS_DRIVE[] = {"01232101", "0123", "0213", "0120", "3210", "02132031"};
static const char *const ARPS_CALM[] = {"0123", "0213", "01", "0120", "0312"};
static const char *const AMBIENT_ARPS[] = {"0123210", "0213", "0123", "3210", "01210", "02310"};
static const int AMBIENT_RATES[] = {2, 3, 4};
static const char *const HARP_ARPS[] = {"01232101", "0123", "02132031", "0121"};
static const char *const WALTZ_ARPS[] = {"012101", "021202", "012321", "020121"};

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

// Room for a note on every sixteenth of an eight-bar period, so a busy hook never loses its cadence.
typedef struct {
    MelodyNote notes[128];
    int count;
} Melody;

static const char *const RHYTHMS_POP[] = {"x..x..x.x...x.x.", "x.x.x...x..x..x.", "x...x.x...x.x...",
                                          "..x.x.x.x...x...", "x..x..x...x.x...", "x.x...x.x.x...x."};
static const char *const RHYTHMS_CHIP[] = {"x.xxx.x.x.xxx.x.", "xx.x.x.xx.x.x.x.", "x.x.xx.xx.x.x.x.",
                                           "x.x.x.xxx.x.x.x."};
static const char *const RHYTHMS_SPARSE[] = {"x.......x...x...", "x...........x...", "x.....x...x.....",
                                             "x.......x.......", "x...x.......x..."};
static const char *const RHYTHMS_LOFI[] = {"x.....x...x.....", "x...x.....x.x...", "x.......x.x.x...",
                                           "x.x.....x.......", "x.....x.x.......", "x...x...x.....x."};
// Trance hooks lean on the 3+3+2 dotted rhythm.
static const char *const RHYTHMS_TRANCE[] = {"x..x..x.x..x..x.", "x..x..x...x..x..", "x.x.x..x..x.x...",
                                             "x..x..x..x..x.x.", "x...x..x..x.x..."};
static const char *const RHYTHMS_WALTZ[] = {"x...x...x...", "x.......x...", "x.x.x...x...", "x...x.x.x...",
                                           "x.....x.x...", "x...x.......", "x.x.x.x.x...", "x.......x.x."};

// One-beat rhythm cells: melodies are assembled a beat at a time from a mood's vocabulary, not drawn from a short list.
static const char *const CELLS[] = {"x...", "x.x.", "..x.", "x..x", "xxxx", "x.xx", "xx.x", "....", "xx..", ".x.x"};
enum { CELLS_LIVELY, CELLS_POP, CELLS_LYRICAL, CELLS_SPARSE, CELLS_NONE };
static const int CELL_WEIGHTS[CELLS_NONE][COUNT(CELLS)] = {
    {1, 3, 1, 2, 2, 3, 2, 0, 1, 1},
    {3, 3, 2, 2, 0, 1, 1, 1, 1, 0},
    {3, 2, 3, 2, 0, 0, 0, 2, 0, 1},
    {4, 1, 2, 1, 0, 0, 0, 3, 0, 0},
};

typedef struct {
    const char *const *rhythms;
    int count, low, high;
    // Which cell vocabulary can build fresh rhythms, or CELLS_NONE to keep to the list.
    int cells;
} MelodyStyle;

// A bar of melody rhythm built from cells; it always starts on the downbeat and holds at least two notes.
static void make_rhythm(Composer *c, int cells, char *out) {
    int beats = c->beats_per_bar, onsets;
    do {
        onsets = 0;
        for (int b = 0; b < beats; b++)
            memcpy(out + b * 4, CELLS[rng_weighted(&c->rng, CELL_WEIGHTS[cells], COUNT(CELLS))], 4);
        out[0] = 'x';
        for (int i = 0; i < beats * 4; i++) onsets += out[i] == 'x';
    } while (onsets < 2);
    out[beats * 4] = 0;
}

static MelodyStyle hook_style(Mood mood) {
    switch (mood) {
    case MOOD_KEYGEN: return (MelodyStyle){RHYTHMS_CHIP, COUNT(RHYTHMS_CHIP), 60, 79, CELLS_LIVELY};
    case MOOD_AMBIENT: return (MelodyStyle){RHYTHMS_SPARSE, COUNT(RHYTHMS_SPARSE), 55, 74, CELLS_SPARSE};
    case MOOD_LOFI: return (MelodyStyle){RHYTHMS_LOFI, COUNT(RHYTHMS_LOFI), 55, 74, CELLS_LYRICAL};
    case MOOD_TRANCE: return (MelodyStyle){RHYTHMS_TRANCE, COUNT(RHYTHMS_TRANCE), 55, 74, CELLS_POP};
    case MOOD_8BIT: return (MelodyStyle){RHYTHMS_CHIP, COUNT(RHYTHMS_CHIP), 62, 81, CELLS_LIVELY};
    case MOOD_DUB: return (MelodyStyle){RHYTHMS_SPARSE, COUNT(RHYTHMS_SPARSE), 55, 72, CELLS_SPARSE};
    case MOOD_DUBSTEP: return (MelodyStyle){RHYTHMS_TRANCE, COUNT(RHYTHMS_TRANCE), 58, 77, CELLS_POP};
    case MOOD_MUSICBOX: return (MelodyStyle){RHYTHMS_WALTZ, COUNT(RHYTHMS_WALTZ), 60, 81, CELLS_LYRICAL};
    case MOOD_16BIT: return (MelodyStyle){RHYTHMS_POP, COUNT(RHYTHMS_POP), 60, 81, CELLS_LYRICAL};
    default: return (MelodyStyle){RHYTHMS_POP, COUNT(RHYTHMS_POP), 55, 74, CELLS_POP};
    }
}

static MelodyStyle verse_style(Mood mood) {
    if (mood == MOOD_MUSICBOX) return (MelodyStyle){RHYTHMS_WALTZ, COUNT(RHYTHMS_WALTZ), 58, 76, CELLS_LYRICAL};
    if (mood == MOOD_8BIT) return (MelodyStyle){RHYTHMS_POP, COUNT(RHYTHMS_POP), 60, 77, CELLS_POP};
    if (mood == MOOD_16BIT) return (MelodyStyle){RHYTHMS_SPARSE, COUNT(RHYTHMS_SPARSE), 57, 74, CELLS_LYRICAL};
    return (MelodyStyle){RHYTHMS_SPARSE, COUNT(RHYTHMS_SPARSE), 53, 70, CELLS_SPARSE};
}

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
        float score = mabs((float)n - target) * c->contour_pull + (leap > 7 ? leap * 6 : leap * c->leap_cost) +
                      (n == previous ? 5 : 0) + rng_float(&c->rng) * 3;
        if ((int)score < best_score) {
            best_score = (int)score;
            best = n;
        }
    }
    return best;
}

// Harmonic minor's raised seventh belongs over its dominant chords; elsewhere the melody walks natural minor.
static Key melody_key(const Composer *c, const Chord *ch) {
    Key k = c->key;
    if (k.scale == SCALE_HARMONIC_MINOR && ch->degree != 4 && ch->degree != 6) k.scale = SCALE_MINOR;
    return k;
}

// Fills the notes between two skeleton tones: passing tones when there's room to walk, neighbors otherwise.
static int fill_note(Composer *c, Key k, int from, int to, int index, int count) {
    int steps = 0, probe = from;
    while (probe != to && abs(steps) < 8) {
        int next = key_step(k, probe, to > probe ? 1 : -1);
        probe = next;
        steps += to > from ? 1 : -1;
    }
    if (abs(steps) >= 2 && abs(steps) <= count + 1) return key_step(k, from, (to > from ? 1 : -1) * (index + 1));
    if (index % 2 == 0) return key_step(k, from, rng_chance(&c->rng, 60) ? 1 : -1);
    return from;
}

// A note a semitone above a chord tone grates when it lingers, so anything longer than a passing sixteenth
// settles onto the nearest chord tone. A semitone below (a major seventh, a ninth over minor) is color and stays.
static int settle(const Composer *c, int start_bar, const MelodyNote *m, int note) {
    const Chord *ch = chord_at(c, start_bar + m->row / c->rows_per_bar, m->row % c->rows_per_bar);
    if (chord_has_pc(ch, note - 1) || (m->len < 2 && (m->row & 1))) return note;
    for (int i = 0; i < ch->count; i++)
        if ((((note - 1 - ch->root_pc - ch->tones[i]) % 12) + 12) % 12 == 1) return chord_nearest(ch, note);
    return note;
}

typedef enum { CADENCE_NONE, CADENCE_HALF, CADENCE_HOME } Cadence;

// Gives pitches to onsets spanning two bars: chord tones on the beats following an arch or a fall,
// stepwise fills between them, and an ending that rests on the fifth or comes home to the tonic.
static void realize(Composer *c, int start_bar, MelodyNote *notes, int count, int low, int high, float rise,
                    int start_note, Cadence cadence) {
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
            MelodyNote *m = &notes[strong[k] + 1 + j];
            const Chord *ch = chord_at(c, start_bar + m->row / rpb, m->row % rpb);
            int n = fill_note(c, melody_key(c, ch), skeleton[k], skeleton[k + 1], j, gap);
            m->note = settle(c, start_bar, m, clamp_note(c, n, low - 2, high + 2));
        }
    }
}

// A two-bar motif and its answer make a phrase; the period plays the phrase twice, coming home the second time.
static void write_period(Composer *c, int start_bar, const MelodyStyle *style, Melody *out) {
    int rpb = c->rows_per_bar, low = style->low, high = style->high;
    char built[2][17];
    const char *a = style->rhythms[rng_range(&c->rng, 0, style->count)];
    const char *b = style->rhythms[rng_range(&c->rng, 0, style->count)];
    if (style->cells != CELLS_NONE && rng_chance(&c->rng, 70)) {
        make_rhythm(c, style->cells, built[0]);
        make_rhythm(c, style->cells, built[1]);
        a = built[0];
        b = built[1];
    }
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
            for (int i = 0; i < count && out->count < COUNT(out->notes); i++) {
                MelodyNote m = phrase[i];
                m.row += (base_bar - start_bar) * rpb;
                out->notes[out->count++] = m;
            }
        }
    }
}

static void play_melody(Composer *c, int start_bar, const Melody *m, int ch, Slot slot, int vol, int octave,
                        int glide) {
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

// The first bars of a melody, for sections too short to hold all eight.
static Melody slice(const Composer *c, const Melody *m, int bars) {
    Melody part = {0};
    for (int i = 0; i < m->count; i++)
        if (m->notes[i].row < bars * c->rows_per_bar) part.notes[part.count++] = m->notes[i];
    return part;
}

// The hook again with its answers re-sung: the call stays recognizable while the reply goes somewhere new.
static void vary_period(Composer *c, int start_bar, const Melody *hook, const MelodyStyle *style, Melody *out) {
    *out = *hook;
    int rpb = c->rows_per_bar;
    for (int half = 0; half < 2; half++) {
        int from_row = (half * 4 + 2) * rpb, to_row = from_row + 2 * rpb, first = -1, count = 0;
        MelodyNote phrase[48];
        for (int i = 0; i < out->count && count < 48; i++)
            if (out->notes[i].row >= from_row && out->notes[i].row < to_row) {
                if (first < 0) first = i;
                phrase[count] = out->notes[i];
                phrase[count++].row -= from_row;
            }
        if (!count) continue;
        int previous = first > 0 ? out->notes[first - 1].note : phrase[0].note;
        c->contour = rng_range(&c->rng, 0, 4);
        realize(c, start_bar + half * 4 + 2, phrase, count, style->low, style->high, -rng_between(&c->rng, 0.4f, 0.8f),
                previous, half ? CADENCE_HOME : CADENCE_HALF);
        for (int i = 0; i < count; i++) out->notes[first + i].note = phrase[i].note;
    }
}

// A second voice a diatonic third under the hook, falling to a chord tone on the beats where a third won't fit.
static void harmony_line(Composer *c, int start_bar, const Melody *m, Melody *out) {
    *out = *m;
    for (int i = 0; i < out->count; i++) {
        MelodyNote *n = &out->notes[i];
        const Chord *ch = chord_at(c, start_bar + n->row / c->rows_per_bar, n->row % c->rows_per_bar);
        int below = key_step(melody_key(c, ch), n->note, -2);
        if (n->row % 4 == 0 && !chord_has_pc(ch, below - 1)) {
            below = n->note - 2;
            while (!chord_has_pc(ch, below - 1)) below--;
        }
        n->note = settle(c, start_bar, n, below);
    }
}

static void hook_in_drop(Composer *c, int b, const Melody *m, int second) {
    Mood mood = c->mood;
    Melody harmony;
    if (second) harmony_line(c, b, m, &harmony);
    switch (mood) {
    case MOOD_AMBIENT:
        play_melody(c, b, m, CH_BELL, SL_BELL, 40, 0, 0);
        if (c->slot[SL_LEAD] && second) play_melody(c, b, m, CH_LEAD, SL_LEAD, 34, -1, 0);
        break;
    case MOOD_DUB:
        if (second) play_melody(c, b, m, CH_BELL, SL_BELL, 26, 0, 0);
        break;
    case MOOD_MUSICBOX:
        play_melody(c, b, m, CH_LEAD, SL_LEAD, 44, 0, 0);
        if (second) {
            play_melody(c, b, &harmony, CH_ECHO, SL_LEAD, 30, 0, 0);
            play_melody(c, b, m, CH_BELL, SL_BELL, 22, -1, 0);
        }
        break;
    case MOOD_DUBSTEP:
        // The bass carries a drop's opening on its own, as brostep does, and the hook joins after it, low in the
        // mix with no bell or harmony over it. A drop too short to have an opening of its own sings throughout.
        if (c->bar_in_section[b] > 0 || section_bars(c, b) < 16)
            play_melody(c, b, m, CH_LEAD, SL_LEAD, second ? 28 : 24, 0, 0);
        break;
    case MOOD_8BIT:
        // Pulse two shadows the hook: an echo three rows behind the first time, the way NES composers faked
        // reverb, and a harmony under it the second.
        play_melody(c, b, m, CH_LEAD, SL_LEAD, 46, 0, 0);
        if (second)
            play_melody(c, b, &harmony, CH_ECHO, SL_LEAD, 28, 0, 0);
        else
            echo_melody(c, b, m, 20);
        break;
    case MOOD_KEYGEN:
        play_melody(c, b, m, CH_LEAD, SL_LEAD, 46, 0, 0);
        echo_melody(c, b, m, 18);
        if (second) play_melody(c, b, &harmony, CH_BELL, SL_LEAD, 26, 0, 0);
        break;
    default:
        play_melody(c, b, m, CH_LEAD, SL_LEAD, 46, 0, mood == MOOD_SYNTHWAVE);
        if (second) {
            play_melody(c, b, &harmony, CH_ECHO, SL_LEAD, 30, 0, 0);
            play_melody(c, b, m, CH_BELL, SL_BELL, 22, 1, 0);
        }
        break;
    }
}

static void hook_in_break(Composer *c, int b, const Melody *m, int first) {
    switch (c->mood) {
    case MOOD_TRANCE: play_melody(c, b, m, CH_BELL, SL_BELL, 38, 0, 0); break;
    case MOOD_LOFI: play_melody(c, b, m, CH_BELL, SL_BELL, 32, 0, 0); break;
    case MOOD_8BIT: play_melody(c, b, m, CH_LEAD, SL_LEAD, 32, -1, 0); break;
    case MOOD_MUSICBOX: play_melody(c, b, m, CH_BELL, SL_BELL, 30, -1, 0); break;
    case MOOD_DUB:
        if (first) play_melody(c, b, m, CH_BELL, SL_BELL, 24, 0, 0);
        break;
    default: play_melody(c, b, m, CH_BELL, SL_BELL, 30, 1, 0); break;
    }
}

static void melodies(Composer *c) {
    Melody hook = {0}, verse = {0}, varied = {0};
    int have_hook = 0, have_verse = 0, have_varied = 0, drops = 0;
    for (int s = 0; s < c->info->section_count; s++) drops += c->info->sections[s].kind == SECTION_DROP;
    Mood mood = c->mood;
    MelodyStyle hs = hook_style(mood), vs = verse_style(mood);
    // Each piece sings in its own part of the range.
    int shift = rng_range(&c->rng, -3, 4);
    hs.low += shift;
    hs.high += shift;
    // Verses carry a melody only where the mood sings in them, some after four bars of groove first.
    int verse_sings = mood != MOOD_TRANCE && mood != MOOD_DUB && mood != MOOD_DUBSTEP;
    int verse_waits = mood == MOOD_SYNTHWAVE || mood == MOOD_LOFI;
    for (int s = 0; s < c->info->section_count; s++) {
        const Section *sec = &c->info->sections[s];
        int end = sec->start_bar + sec->bars;
        int from = sec->start_bar + (sec->kind == SECTION_VERSE && verse_waits ? 4 : 0);
        for (int b = from; b + 4 <= end; b += 8) {
            int bars = end - b < 8 ? end - b : 8, first = b == sec->start_bar;
            switch (sec->kind) {
            case SECTION_DROP: {
                if (!have_hook) {
                    write_period(c, b, &hs, &hook);
                    have_hook = 1;
                }
                // The back half's drops sing the hook's answers anew, under a second voice. A piece with a single
                // drop gives it that full arrangement, or moods that save their melody for the second would never sing.
                int second = b >= c->half_bar;
                if (second && !have_varied) {
                    vary_period(c, b, &hook, &hs, &varied);
                    have_varied = 1;
                }
                Melody part = slice(c, second ? &varied : &hook, bars);
                hook_in_drop(c, b, &part, second || drops == 1);
                break;
            }
            case SECTION_VERSE: {
                if (!verse_sings) break;
                if (!have_verse) {
                    write_period(c, b, &vs, &verse);
                    have_verse = 1;
                }
                Melody part = slice(c, &verse, bars);
                if (mood == MOOD_AMBIENT)
                    play_melody(c, b, &part, CH_BELL, SL_BELL, 32, 0, 0);
                else
                    play_melody(c, b, &part, CH_LEAD, SL_LEAD, mood == MOOD_MUSICBOX ? 38 : 36, 0, 0);
                break;
            }
            case SECTION_BREAK: {
                // Trance unveils its hook in the breakdown, before any drop has played it.
                if (!have_hook && mood == MOOD_TRANCE) {
                    write_period(c, b, &hs, &hook);
                    have_hook = 1;
                }
                if (!have_hook) break;
                Melody part = slice(c, &hook, bars);
                hook_in_break(c, b, &part, first);
                break;
            }
            case SECTION_INTRO: {
                // A music box starts alone, with a phrase of its own over the intro's chords.
                if (mood != MOOD_MUSICBOX) break;
                Melody intro;
                write_period(c, b, &hs, &intro);
                Melody part = slice(c, &intro, bars);
                play_melody(c, b, &part, CH_LEAD, SL_LEAD, 40, 0, 0);
                break;
            }
            case SECTION_OUTRO: {
                if (mood != MOOD_MUSICBOX || !have_hook || !first) break;
                Melody part = slice(c, &hook, bars < 4 ? bars : 4);
                play_melody(c, b, &part, CH_LEAD, SL_LEAD, 36, 0, 0);
                break;
            }
            default: break;
            }
        }
    }
}

/* ---------- Effects and automation ---------- */

static void effects(Composer *c) {
    // Cymbals and sweeps borrow whichever channel the piece leaves free.
    int crash_ch = c->slot[SL_TOM] ? CH_PERC : CH_TOM, sweep_ch = c->slot[SL_TEXTURE] ? CH_TOM : CH_TEXTURE;
    for (int s = 0; s < c->info->section_count; s++) {
        const Section *sec = &c->info->sections[s];
        int start = sec->start_bar;
        if (sec->kind == SECTION_DROP) {
            put(c, start, 0, CH_FX, DRUM_NOTE, SL_IMPACT, 50, FX_NONE, 0);
            put(c, start, 0, crash_ch, DRUM_NOTE, SL_CRASH, 48, FX_NONE, 0);
        }
        if (sec->kind == SECTION_BUILD && sec->bars >= 2) {
            put(c, start + sec->bars - 2, 0, CH_FX, DRUM_NOTE, SL_RISER, 44, FX_NONE, 0);
            put(c, start + sec->bars - 1, 0, sweep_ch, DRUM_NOTE, SL_REVERSE, 40, FX_NONE, 0);
        }
        if (sec->kind == SECTION_BREAK) put(c, start, 0, CH_FX, DRUM_NOTE, SL_DOWN, 40, FX_NONE, 0);
        if (sec->kind == SECTION_INTRO && c->slot[SL_TEXTURE])
            put(c, start, 0, CH_TEXTURE, DRUM_NOTE, SL_TEXTURE, 40, FX_NONE, 0);
    }
    // Lo-fi's crackle runs to the very last bar; other textures bow out two bars early.
    if (c->slot[SL_TEXTURE]) put_off(c, c->bars - (c->mood == MOOD_LOFI ? 1 : 2), 0, CH_TEXTURE);
}

// Where a section sits in the song's dynamics, in decibels under a drop; calm moods keep half the range.
static float section_db(const Composer *c, SectionKind kind, float t) {
    float db;
    switch (kind) {
    case SECTION_INTRO: db = -5 + 2 * t; break;
    case SECTION_VERSE: db = -4; break;
    case SECTION_BUILD: db = -4.5f + 3 * t * t; break;
    case SECTION_BREAK: db = -5; break;
    case SECTION_OUTRO: db = -3 - 6 * smoothstep(0.3f, 1, t); break;
    default: db = 0; break;
    }
    int calm = c->mood == MOOD_AMBIENT || c->mood == MOOD_MUSICBOX || c->mood == MOOD_LOFI;
    return calm ? db * 0.5f : db;
}

static void automation(Composer *c) {
    Song *s = c->song;
    int rows = c->bars * c->rows_per_bar;
    s->automation_rows = rows;
    s->duck_amount = malloc(sizeof(float) * (size_t)rows);
    s->master_cutoff = malloc(sizeof(float) * (size_t)rows);
    s->level = malloc(sizeof(float) * (size_t)rows);
    if (!s->duck_amount || !s->master_cutoff || !s->level) {
        s->automation_rows = 0;
        return;
    }
    float depth = MOODS[c->mood].mix.duck_depth;
    // Lo-fi stays a little muffled throughout, as if played off an old tape.
    float ceiling = c->mood == MOOD_LOFI ? 0.7f + 0.1f * rng_float(&c->rng) : 1;
    for (int r = 0; r < rows; r++) {
        int bar = r / c->rows_per_bar;
        const Section *sec = &c->info->sections[c->section_of[bar]];
        float t = (float)(r - sec->start_bar * c->rows_per_bar) / (float)(sec->bars * c->rows_per_bar);
        // Drops pump harder than anything else, which is most of what makes them feel like drops.
        float duck = mmin(0.9f, depth * (sec->kind == SECTION_DROP ? 1.3f : sec->kind == SECTION_BREAK ? 0.15f : 0.5f));
        float cutoff = 1;
        switch (sec->kind) {
        case SECTION_INTRO: cutoff = c->mood == MOOD_DUB ? 0.45f + 0.55f * t : 0.55f + 0.45f * t * t; break;
        case SECTION_BUILD: cutoff = 0.5f + 0.5f * t * t; break;
        case SECTION_BREAK:
            if (c->mood == MOOD_TRANCE) cutoff = 0.62f + 0.38f * t;
            break;
        case SECTION_OUTRO: cutoff = 1 - 0.35f * smoothstep(0.4f, 1.0f, t); break;
        default: break;
        }
        if (c->mood == MOOD_AMBIENT) cutoff = mmax(cutoff, 0.8f);
        if (c->mood == MOOD_MUSICBOX) cutoff = mmax(cutoff, 0.85f);
        cutoff = mmin(cutoff, ceiling);
        s->duck_amount[r] = duck;
        s->master_cutoff[r] = cutoff;
        s->level[r] = mexp2(section_db(c, sec->kind, t) / 6.0206f);
    }
}

static void ending(Composer *c) {
    int bar = c->bars - 1;
    const Chord *ch = &c->chord_of[bar][0];
    if (c->slot[SL_BELL]) put(c, bar, 0, CH_BELL, 1 + 60 + ch->root_pc, SL_BELL, 40, FX_NONE, 0);
    if (c->slot[SL_PAD]) {
        int voicing[4];
        chord_voice(ch, c->pad_voices, 41, 68, NULL, voicing);
        for (int v = 0; v < c->pad_voices; v++) put(c, bar, 0, c->pad_channel + v, voicing[v], SL_PAD, 46, FX_NONE, 0);
    }
    for (int ch_i = CH_BASS; ch_i <= CH_BELL; ch_i++) put_off(c, bar, c->rows_per_bar - 1, ch_i);
    put_off(c, bar, c->rows_per_bar - 1, CH_SUB);
}

/* ---------- The whole piece ---------- */

static Mood pick_mood(Rng *r) {
    static const int weights[MOOD_COUNT] = {[MOOD_KEYGEN] = 11, [MOOD_AMBIENT] = 11, [MOOD_SYNTHWAVE] = 11,
                                            [MOOD_LOFI] = 11,   [MOOD_TRANCE] = 11,  [MOOD_8BIT] = 11,
                                            [MOOD_MUSICBOX] = 11, [MOOD_DUB] = 11,   [MOOD_DUBSTEP] = 12};
    return (Mood)rng_weighted(r, weights, MOOD_COUNT);
}

// The back half changes just two or three textures, never the kick or bass, so the piece develops instead of
// turning into another song.
static void vary_back_half(Composer *c) {
    static const GrooveSlot textures[] = {G_SNARE, G_HATS,   G_HATS_CALM,   G_ARP,  G_ARP_CALM,
                                          G_ARP_RATE, G_CHORDS, G_CHORDS_CALM, G_FILL, G_FLAG2};
    GrooveSlot open[COUNT(textures)];
    int n = 0, varied[G_COUNT] = {0};
    for (int i = 0; i < COUNT(textures); i++)
        if (c->groove[textures[i]][0] != c->groove[textures[i]][1]) open[n++] = textures[i];
    for (int k = rng_range(&c->rng, 2, 4); k > 0 && n > 0; k--) {
        int i = rng_range(&c->rng, 0, n);
        varied[open[i]] = 1;
        open[i] = open[--n];
    }
    for (int s = 0; s < G_COUNT; s++)
        if (!varied[s]) c->groove[s][1] = c->groove[s][0];
}

// Each groove choice is drawn once per piece, and only by the moods that use it.
static void pick_groove(Composer *c) {
    switch (c->mood) {
    case MOOD_KEYGEN:
        pick_pair(c, G_KICK, COUNT(CHIP_KICKS));
        pick_pair(c, G_KICK_CALM, COUNT(CHIP_KICKS_CALM));
        pick_pair(c, G_HATS, COUNT(CHIP_HATS));
        pick_pair(c, G_HATS_CALM, COUNT(CHIP_HATS_CALM));
        pick_pair(c, G_BASS, COUNT(CHIP_BASS));
        pick_pair(c, G_CHORDS, COUNT(CHIP_STABS));
        pick_pair(c, G_CHORDS_CALM, COUNT(CHIP_STABS_CALM));
        break;
    case MOOD_AMBIENT:
        pick_pair(c, G_KICK, COUNT(AMBIENT_KICKS));
        pick_pair(c, G_HATS, COUNT(AMBIENT_SHAKERS));
        pick_pair(c, G_ARP, COUNT(AMBIENT_ARPS));
        pick_pair(c, G_ARP_CALM, COUNT(AMBIENT_ARPS));
        pick_pair(c, G_ARP_RATE, COUNT(AMBIENT_RATES));
        break;
    case MOOD_LOFI:
        pick_pair(c, G_KICK, COUNT(LOFI_KICKS));
        pick_pair(c, G_FILL, COUNT(LOFI_KICKS) - 1);
        pick_pair(c, G_SNARE, COUNT(LOFI_SNARES));
        pick_pair(c, G_HATS, COUNT(LOFI_HATS));
        pick_pair(c, G_CHORDS, COUNT(LOFI_COMPS));
        pick_pair(c, G_BASS, COUNT(LOFI_BASS));
        break;
    case MOOD_TRANCE:
        pick_pair(c, G_CHORDS, COUNT(GATES));
        pick_pair(c, G_FLAG, 2);
        pick_pair(c, G_ARP, COUNT(ARPS_DRIVE));
        pick_pair(c, G_ARP_CALM, COUNT(ARPS_CALM));
        break;
    case MOOD_8BIT:
        pick_pair(c, G_BASS, COUNT(TRIANGLE_DRIVE));
        pick_pair(c, G_BASS_CALM, COUNT(TRIANGLE_CALM));
        pick_pair(c, G_HATS, COUNT(NOISE_DRIVE));
        pick_pair(c, G_HATS_CALM, COUNT(NOISE_CALM));
        pick_pair(c, G_CHORDS, COUNT(PULSE_ARPS));
        break;
    case MOOD_MUSICBOX: pick_pair(c, G_ARP, COUNT(WALTZ_ARPS)); break;
    case MOOD_16BIT:
        pick_pair(c, G_KICK, COUNT(MARCH_KICKS));
        pick_pair(c, G_HATS, COUNT(MARCH_HATS));
        pick_pair(c, G_BASS, COUNT(SLAP_DRIVE));
        pick_pair(c, G_BASS_CALM, COUNT(SLAP_CALM));
        pick_pair(c, G_ARP, COUNT(HARP_ARPS));
        break;
    case MOOD_DUBSTEP:
        pick_pair(c, G_KICK, COUNT(STEP_KICKS));
        pick_pair(c, G_HATS, COUNT(STEP_HATS));
        pick_pair(c, G_HATS_CALM, COUNT(STEP_HATS));
        pick_pair(c, G_BASS, COUNT(WOBBLES));
        pick_pair(c, G_BASS_CALM, COUNT(WOBBLES));
        pick_pair(c, G_FILL, COUNT(WOBBLE_FILLS));
        break;
    case MOOD_DUB:
        pick_pair(c, G_SNARE, COUNT(DUB_RIMS));
        pick_pair(c, G_CHORDS, COUNT(DUB_STABS));
        pick_pair(c, G_CHORDS_CALM, COUNT(DUB_STABS));
        pick_pair(c, G_BASS, COUNT(DUB_BASS));
        break;
    default:
        pick_pair(c, G_KICK, COUNT(SYNTH_KICKS));
        pick_pair(c, G_KICK_CALM, COUNT(SYNTH_KICKS_CALM));
        pick_pair(c, G_SNARE, COUNT(SYNTH_SNARES));
        pick_pair(c, G_HATS, COUNT(SYNTH_HATS));
        pick_pair(c, G_HATS_CALM, COUNT(SYNTH_HATS_CALM));
        pick_pair(c, G_BASS, COUNT(SYNTH_BASS));
        pick_pair(c, G_BASS_CALM, COUNT(SYNTH_BASS_CALM));
        pick_pair(c, G_ARP, COUNT(ARPS_DRIVE));
        pick_pair(c, G_ARP_CALM, COUNT(ARPS_CALM));
        pick_pair(c, G_ARP_RATE, 2);
        break;
    }
    vary_back_half(c);
}

static void write_parts(Composer *c) {
    pick_groove(c);
    for (int bar = 0; bar < c->bars; bar++) {
        SectionKind k = c->kind_of[bar];
        switch (c->mood) {
        case MOOD_KEYGEN:
            drums_keygen(c, bar);
            chip_chords(c, bar);
            break;
        case MOOD_AMBIENT:
            drums_ambient(c, bar);
            pads(c, bar, CH_PAD, 100);
            if (k != SECTION_INTRO || c->bar_in_section[bar] >= 4)
                arpeggio(c, bar, AMBIENT_ARPS[groove(c, k == SECTION_DROP ? G_ARP : G_ARP_CALM, bar)],
                         AMBIENT_RATES[groove(c, G_ARP_RATE, bar)], -1, 30);
            break;
        case MOOD_LOFI:
            drums_lofi(c, bar);
            keys_lofi(c, bar);
            break;
        case MOOD_TRANCE:
            drums_trance(c, bar);
            // Drops keep a quieter pad sustaining under the gated stabs, so they never lose their body.
            if (k == SECTION_DROP) gates(c, bar);
            if (k != SECTION_INTRO) pads(c, bar, CH_PAD, k == SECTION_DROP ? 70 : 100);
            if (k == SECTION_VERSE || k == SECTION_DROP || (k == SECTION_BUILD && !section_last_bar(c, bar)))
                arpeggio(c, bar,
                         k == SECTION_DROP ? ARPS_DRIVE[groove(c, G_ARP, bar)] : ARPS_CALM[groove(c, G_ARP_CALM, bar)],
                         1, 0, 28);
            break;
        case MOOD_8BIT:
            drums_8bit(c, bar);
            pulse_chords(c, bar);
            break;
        case MOOD_MUSICBOX:
            waltz(c, bar);
            if (k == SECTION_DROP)
                pads(c, bar, CH_PAD, 100);
            else if (bar > 0 && c->kind_of[bar - 1] == SECTION_DROP)
                release_pads(c, bar, CH_PAD);
            if (k == SECTION_DROP || k == SECTION_BREAK)
                arpeggio(c, bar, WALTZ_ARPS[groove(c, G_ARP, bar)], 2, -1, k == SECTION_BREAK ? 30 : 24);
            break;
        case MOOD_16BIT:
            drums_16bit(c, bar);
            pads(c, bar, CH_PAD, k == SECTION_DROP ? 80 : 100);
            arpeggio(c, bar, HARP_ARPS[groove(c, G_ARP, bar)], k == SECTION_DROP ? 1 : 2, 0,
                     k == SECTION_DROP ? 22 : 28);
            orch_hits(c, bar);
            break;
        case MOOD_DUBSTEP:
            drums_dubstep(c, bar);
            pads(c, bar, CH_PAD, k == SECTION_DROP ? 50 : 100);
            break;
        case MOOD_DUB:
            drums_dub(c, bar);
            stabs(c, bar);
            if (k == SECTION_BREAK)
                pads(c, bar, CH_LEAD, 100);
            else if (bar > 0 && c->kind_of[bar - 1] == SECTION_BREAK)
                release_pads(c, bar, CH_LEAD);
            break;
        default:
            drums_synthwave(c, bar);
            pads(c, bar, CH_PAD, 100);
            if (k == SECTION_VERSE || k == SECTION_DROP || (k == SECTION_BUILD && !section_last_bar(c, bar)))
                arpeggio(c, bar,
                         k == SECTION_DROP ? ARPS_DRIVE[groove(c, G_ARP, bar)] : ARPS_CALM[groove(c, G_ARP_CALM, bar)],
                         k == SECTION_DROP ? 1 : 1 + groove(c, G_ARP_RATE, bar), 0, 30);
            break;
        }
        bass_part(c, bar);
        sub_part(c, bar);
    }
    melodies(c);
    effects(c);
    drop_fills(c);
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
    c->salt = hash_u32((uint32_t)seed ^ (uint32_t)(seed >> 32));
    if (mood < 0 || mood >= MOOD_COUNT) {
        mood = pick_mood(&c->rng);
        // 16-bit takes its tenth by the seed's own hash, not the draw, so every other seed keeps its mood; this salt
        // also leaves every voyage reviewed so far where it was.
        if (hash_u32(c->salt ^ 0x16b23u) % 10 == 0) mood = MOOD_16BIT;
    }
    const MoodSpec *spec = &MOODS[mood];
    SongInfo *info = &out->info;
    seed_normalize(title ? title : "", info->title, sizeof info->title);
    c->song = song;
    c->info = info;
    c->mood = mood;
    info->mood = mood;
    info->bpm = song->bpm = rng_range(&c->rng, spec->bpm_lo, spec->bpm_hi + 1);
    song->speed = 6;
    c->beats_per_bar = info->beats_per_bar = spec->meter ? spec->meter : 4;
    c->rows_per_bar = info->rows_per_bar = c->beats_per_bar * 4;
    c->bars_per_pattern = 4;
    song->pattern_rows = c->rows_per_bar * c->bars_per_pattern;
    c->swing = spec->swing_hi > 0 ? rng_range(&c->rng, spec->swing_lo, spec->swing_hi + 1) : 0;
    c->swing_eighths = spec->swing_eighths;
    info->bar_seconds = c->rows_per_bar * song->speed * 2.5 / song->bpm;
    c->key.tonic = rng_range(&c->rng, 0, 12);
    c->key.scale = (Scale)rng_weighted(&c->rng, spec->scales, SCALE_COUNT);
    info->key = c->key;
    c->pad_voices = mood == MOOD_AMBIENT ? 4 : 3;
    c->leap_cost = rng_between(&c->rng, 0.45f, 1.4f);
    c->contour_pull = rng_between(&c->rng, 1.0f, 2.0f);
    c->pad_channel = mood == MOOD_DUB ? CH_LEAD : CH_PAD;
    plan_form(c, length);
    info->bars = c->bars;
    // The back half begins once the first drop has played.
    c->half_bar = c->bars / 2;
    for (int s = 0; s < info->section_count; s++)
        if (info->sections[s].kind == SECTION_DROP) {
            c->half_bar = info->sections[s].start_bar + info->sections[s].bars;
            break;
        }
    if (rng_chance(&c->rng, (int)spec->lift_chance))
        for (int s = info->section_count - 1; s > 0; s--)
            if (info->sections[s].kind == SECTION_DROP) {
                info->lift = rng_chance(&c->rng, 50) ? 1 : 2;
                info->lift_bar = info->sections[s].start_bar;
                // A build's last bar holds the dominant: lifting it too makes it the new key's, so the drop lands home.
                if (info->sections[s - 1].kind == SECTION_BUILD) info->lift_bar--;
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
    vary_mix(c, &song->mix);
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
