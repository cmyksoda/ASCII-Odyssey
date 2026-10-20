// SPDX-License-Identifier: GPL-3.0-only
#ifndef SCENE_H
#define SCENE_H

#include "ascii.h"
#include "palette.h"
#include "score.h"
#include "stroke.h"

// Everything a scene may draw with, for one moment of one piece.
typedef struct {
    // Seconds into the piece, and into the current shot.
    float t, local, length;
    float energy;
    // How strongly the picture answers the music right now: 1 in reactive scenes, 0 in still ones.
    float react;
    // The backup plan's per-section reactivity, used instead when the direction asks for it.
    float section_react;
    const Score *score;
    const Palette *palette;
    Canvas *canvas;
    Grid *grid;
    Stroker *stroker;
    AsciiMatcher *matcher;
    FrameFx *fx;
    // Reduce flashing, reduce motion.
    int calm, still;
    // Recent stereo output around t, for scopes; may be NULL.
    const float *scope;
    int scope_frames;
} Frame;

#define SCENE_STATE 512

// A scene draws a whole frame into the grid as a pure function of the Frame; setup picks its look for one shot.
typedef struct {
    const char *name;
    // Bit per Mood that features it.
    unsigned moods;
    // How busy and bright it is, from 0 (calm) to 1 (intense), matched against the music's energy.
    float intensity;
    // Reactive scenes answer the music all the way through; the rest never do.
    int reactive;
    void (*setup)(void *state, Rng *r, const Frame *f);
    void (*draw)(void *state, const Frame *f);
} SceneKind;

extern const SceneKind scene_kinds[];
extern const int scene_kind_count;

#define MOOD_BIT(m) (1u << (m))
#define ALL_MOODS ((1u << MOOD_COUNT) - 1)

// Resolves the canvas into the grid with the frame's matcher.
void frame_resolve(const Frame *f);
// A color moved toward the ground by k (k > 1 moves it further out): dimmer on a dark ground, paler on a light one, so
// distance and fading read the same way on paper as on night sky.
Rgbf frame_dim(const Frame *f, Rgbf color, float k);
// Beat pulses in reactive scenes (none in still ones), softer for viewers who asked for calmer pictures.
float frame_pulse(const Frame *f, Role role, float tau, float strength);
// Distance covered since the shot began at a base speed plus a share that follows the music's energy.
float frame_travel(const Frame *f, float base, float with_energy);
// Distance added by a role's hits since the shot began: each one a lunge, softer when calm.
float frame_surge(const Frame *f, Role role, float tau, float strength);

/* Drawing helpers every scene shares. Scene units run -aspect..aspect across and -1..1 up the screen. */

void put_cell(Grid *g, int col, int row, int glyph, Rgbf color);
// Writes only over blank cells, for things that sit behind the rest of the picture.
void put_cell_under(Grid *g, int col, int row, int glyph, Rgbf color);
int cell_col(const Grid *g, float x);
int cell_row(const Grid *g, float y);
// A faint field of far stars drifting sideways.
void dust_stars(const Frame *f, uint32_t salt, float drift, float level);
// A role's notes from t back to t - span, newest first.
int recent_notes(const Score *s, Role role, float t, float span, const ScoreEvent **out, int max);
// An out-of-focus light: brightest in the middle and fading to nothing at its rim.
void bokeh(Canvas *c, float x, float y, float radius, float level, Rgbf color);
// A smooth field computed only at the grid's cell corners and blended across each cell's samples: an eighth of the
// work of computing every sample, for fields too soft to tell the difference.
typedef void (*CornerFn)(const void *ctx, const Frame *f, float x, float y, float *ink, Rgbf *color);
void canvas_from_corners(const Frame *f, CornerFn fn, const void *ctx);
// A moon or sun drawn as line art: a bright rim, a few soft craters inside, and a faint halo around it. Behind, it
// only fills cells still blank, so whatever was drawn first passes in front of it.
void moon_disc(const Frame *f, float x, float y, float radius, Rgbf color, uint32_t salt, int behind);

/* Scenes that live in their own files, listed in the table in scenes.c. */

#define SCENE(name)                                                                                                     \
    void name##_setup(void *state, Rng *r, const Frame *f);                                                             \
    void name##_draw(void *state, const Frame *f);
SCENE(kaleidoscope)
SCENE(spectrum)
SCENE(ripples)
SCENE(fireworks)
SCENE(lasers)
SCENE(lissajous)
SCENE(skyline)
SCENE(digital_rain)
SCENE(checker)
SCENE(petals)
SCENE(lava)
SCENE(clouds)
SCENE(lanterns)
SCENE(jellyfish)
SCENE(terrain)
SCENE(galaxy)
SCENE(fireflies)
SCENE(ocean)
#undef SCENE

#endif
