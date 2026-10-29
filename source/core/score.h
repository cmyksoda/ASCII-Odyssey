// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SCORE_H
#define SCORE_H

#include "compose.h"

// Every note of a composition with its exact time, so pictures can answer to the music as a pure
// function of the clock: nothing is measured from the audio while it plays.
typedef struct {
    float time, velocity;
    uint8_t role, note, channel;
} ScoreEvent;

typedef struct {
    int count;
    ScoreEvent *events;
    int *by_role[ROLE_COUNT];
    int role_count[ROLE_COUNT];
    const SongInfo *info;
    float beat, length;
    // Running integral of the energy curve at 10 Hz: distances travelled grow with the music and never shrink.
    float *energy_area;
    int area_count;
    // Velocities summed along each role's notes, so pulse integrals cost a few terms instead of every note.
    float *velocity_sum[ROLE_COUNT];
} Score;

int score_build(Score *s, const Composition *c);
void score_free(Score *s);
// Seconds since this role last played at or before t; a large number before its first note.
float score_since(const Score *s, Role role, float t);
// The last note's velocity, fading as exp(-elapsed / tau).
float score_pulse(const Score *s, Role role, float t, float tau);
const ScoreEvent *score_last(const Score *s, Role role, float t);
int score_count(const Score *s, Role role, float from, float to);
const Section *score_section(const Score *s, float t);
const ChordMark *score_chord(const Score *s, float t);
// Section energy, eased across section boundaries over a beat.
float score_energy(const Score *s, float t);
// The integral of energy from 0 to t, for motion whose speed follows the music.
float score_energy_area(const Score *s, float t);
// The integral of score_pulse from 0 to t: a lunge forward on every hit that never slides back.
float score_pulse_area(const Score *s, Role role, float t, float tau);

#endif
