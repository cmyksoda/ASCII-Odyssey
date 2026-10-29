// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AUDIO_TOOLS_H
#define AUDIO_TOOLS_H

typedef struct {
    double seconds, peak_db, rms_db, lufs, loudest_short_lufs;
    long clipped, nonfinite, big_jumps;
} AudioStats;

int wav_write(const char *path, const float *stereo, long frames);
void audio_measure(const float *stereo, long frames, AudioStats *out);
int spectrogram_png(const char *path, const float *stereo, long frames, int width, int height);

#endif
