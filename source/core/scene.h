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
    // How strongly the picture answers the music right now, 0 to 1.
    float react;
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
    float energy_lo, energy_hi;
    void (*setup)(void *state, Rng *r, const Frame *f);
    void (*draw)(void *state, const Frame *f);
} SceneKind;

extern const SceneKind scene_kinds[];
extern const int scene_kind_count;

#define MOOD_BIT(m) (1u << (m))
#define ALL_MOODS 0xffu

// Resolves the canvas into the grid with the frame's matcher.
void frame_resolve(const Frame *f);
// Beat pulses, as strong as the moment's reactivity allows, and softer for viewers who asked for calmer pictures.
float frame_pulse(const Frame *f, Role role, float tau, float strength);
// Distance covered since the shot began at a base speed plus a share that follows the music's energy.
float frame_travel(const Frame *f, float base, float with_energy);
// Distance added by a role's hits since the shot began: each one a lunge, softer when calm.
float frame_surge(const Frame *f, Role role, float tau, float strength);

#endif
