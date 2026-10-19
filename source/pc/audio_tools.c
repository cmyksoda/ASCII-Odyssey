// SPDX-License-Identifier: GPL-3.0-only
// Measuring audio instead of arguing with ears: WAV out, BS.1770 loudness, clip and jump counts, spectrograms.
#include "audio_tools.h"

#include "../core/song.h"
#include "softgfx.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put16(FILE *f, int v) {
    fputc(v & 255, f);
    fputc((v >> 8) & 255, f);
}

static void put32le(FILE *f, long v) {
    put16(f, (int)(v & 0xffff));
    put16(f, (int)((v >> 16) & 0xffff));
}

int wav_write(const char *path, const float *stereo, long frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite("RIFF", 1, 4, f);
    put32le(f, 36 + frames * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put32le(f, 16);
    put16(f, 1);
    put16(f, 2);
    put32le(f, AUDIO_RATE);
    put32le(f, AUDIO_RATE * 4);
    put16(f, 4);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32le(f, frames * 4);
    for (long i = 0; i < frames * 2; i++) {
        float v = stereo[i] * 32767.0f;
        int s = v > 32767 ? 32767 : v < -32768 ? -32768 : (int)lrintf(v);
        put16(f, s);
    }
    fclose(f);
    return 0;
}

typedef struct {
    double b0, b1, b2, a1, a2, z1, z2;
} Biquad;

static double biquad(Biquad *q, double x) {
    double y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

void audio_measure(const float *stereo, long frames, AudioStats *out) {
    memset(out, 0, sizeof *out);
    out->seconds = (double)frames / AUDIO_RATE;
    double peak = 0, sum = 0;
    // K-weighting at 48 kHz, straight from ITU-R BS.1770.
    Biquad shelf[2], high[2];
    for (int c = 0; c < 2; c++) {
        shelf[c] = (Biquad){1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585, 0, 0};
        high[c] = (Biquad){1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621, 0, 0};
    }
    long step = AUDIO_RATE / 10, blocks = frames / step;
    double *power = calloc((size_t)blocks + 1, sizeof(double));
    float prev[2] = {0, 0};
    for (long i = 0; i < frames; i++)
        for (int c = 0; c < 2; c++) {
            float v = stereo[i * 2 + c];
            if (!isfinite(v)) {
                out->nonfinite++;
                v = 0;
            }
            double a = fabs(v);
            if (a > peak) peak = a;
            if (a >= 0.999) out->clipped++;
            if (fabs(v - prev[c]) > 0.37) out->big_jumps++;
            prev[c] = v;
            sum += (double)v * v;
            double k = biquad(&high[c], biquad(&shelf[c], v));
            if (power) power[i / step] += k * k;
        }
    out->peak_db = 20 * log10(peak + 1e-12);
    out->rms_db = 10 * log10(sum / (frames * 2.0) + 1e-12);
    // 400 ms blocks overlapping by 75%, gated at -70 LUFS, then 10 LU under their mean.
    double gated = 0, loudest = -200;
    int count = 0;
    double *loud = calloc((size_t)blocks + 1, sizeof(double));
    for (long b = 0; power && loud && b + 3 < blocks; b++) {
        double ms = (power[b] + power[b + 1] + power[b + 2] + power[b + 3]) / (4.0 * step);
        loud[b] = -0.691 + 10 * log10(ms + 1e-12);
        if (loud[b] > loudest) loudest = loud[b];
        if (loud[b] > -70) {
            gated += ms;
            count++;
        }
    }
    double relative = count ? -0.691 + 10 * log10(gated / count) - 10 : -70;
    gated = 0;
    count = 0;
    for (long b = 0; power && loud && b + 3 < blocks; b++)
        if (loud[b] > -70 && loud[b] > relative) {
            gated += (power[b] + power[b + 1] + power[b + 2] + power[b + 3]) / (4.0 * step);
            count++;
        }
    out->lufs = count ? -0.691 + 10 * log10(gated / count) : -70;
    out->loudest_short_lufs = loudest;
    free(power);
    free(loud);
}

static void fft(double *re, double *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) {
                double wr = cos(a * k), wi = sin(a * k);
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k] = ur + vr;
                im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr;
                im[i + k + len / 2] = ui - vi;
            }
    }
}

static void heat(double t, uint8_t *rgb) {
    static const double stops[6][3] = {{0, 0, 4}, {40, 11, 84}, {120, 28, 109}, {196, 60, 78}, {245, 140, 34}, {252, 255, 164}};
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    double x = t * 5;
    int i = (int)x;
    if (i > 4) i = 4;
    double f = x - i;
    for (int k = 0; k < 3; k++) rgb[k] = (uint8_t)(stops[i][k] + (stops[i + 1][k] - stops[i][k]) * f);
}

int spectrogram_png(const char *path, const float *stereo, long frames, int width, int height) {
    const int n = 4096;
    Image im;
    if (image_init(&im, width, height) != 0) return -1;
    double *re = malloc(sizeof(double) * n), *im_ = malloc(sizeof(double) * n);
    for (int x = 0; x < width && re && im_; x++) {
        long start = (long)((double)x / width * (frames - n));
        for (int i = 0; i < n; i++) {
            long k = start + i;
            double v = k >= 0 && k < frames ? 0.5 * (stereo[k * 2] + stereo[k * 2 + 1]) : 0;
            re[i] = v * (0.5 - 0.5 * cos(2 * M_PI * i / (n - 1)));
            im_[i] = 0;
        }
        fft(re, im_, n);
        for (int y = 0; y < height; y++) {
            double hz = 30 * pow(18000.0 / 30, (double)(height - 1 - y) / (height - 1));
            int bin = (int)(hz / AUDIO_RATE * n);
            if (bin >= n / 2) bin = n / 2 - 1;
            double mag = sqrt(re[bin] * re[bin] + im_[bin] * im_[bin]) / (n / 4);
            heat((20 * log10(mag + 1e-9) + 100) / 90, &im.rgb[(y * width + x) * 3]);
        }
    }
    int rc = png_write(path, &im);
    free(re);
    free(im_);
    image_free(&im);
    return rc;
}
