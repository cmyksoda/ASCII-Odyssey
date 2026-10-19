// SPDX-License-Identifier: GPL-3.0-only
// One ASND stereo voice fed by its own thread, after PaperAirplaneChase/dev/AUDIO.md: buffers of two whole
// DSP mix cycles, never reused while the DSP may read them, refusals kept and retried, a dead voice restarted.
#include "audio.h"

#include "../core/keyboard.h"

#include <asndlib.h>
#include <gccore.h>
#include <malloc.h>
#include <stdlib.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>
#include <unistd.h>

#define FRAMES 2048
#define BUFFERS 3
#define BYTES (FRAMES * 4)

static int16_t buffers[BUFFERS][FRAMES * 2] ATTRIBUTE_ALIGN(32);
static float mix[FRAMES * 2];
static unsigned char stack[64 * 1024] ATTRIBUTE_ALIGN(32);
static lwp_t thread;
static mutex_t lock;
static volatile int running, started_thread;
static int voice_started, pending = -1, paused;
static uint64_t completed, segment;
static Player *player;
static AudioStatus status;
static int volume = 200;

void audio_init(void) {
    ASND_Init();
    ASND_Pause(0);
    LWP_MutexInit(&lock, false);
}

static void fill(int16_t *out) {
    u64 t0 = gettime();
    int got = player ? player_render(player, mix, FRAMES) : 0;
    for (int i = got * 2; i < FRAMES * 2; i++) mix[i] = 0;
    for (int i = 0; i < FRAMES * 2; i++) {
        float v = mix[i] * 32767.0f;
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    DCFlushRange(out, BYTES);
    float load = (float)ticks_to_microsecs(gettime() - t0) / (FRAMES * 1e6f / AUDIO_RATE);
    status.load_avg += (load - status.load_avg) * 0.05f;
    if (load > status.load_max) status.load_max = load;
}

// ASND calls happen under the lock with buffers already mixed; mixing itself runs outside it,
// so the main thread never waits on the mixer to read the playback position.
static int service(void) {
    if (!voice_started || ASND_StatusVoice(0) == SND_UNUSED) {
        if (pending < 0) return -1;
        if (voice_started) {
            completed += segment;
            status.restarts++;
        }
        ASND_SetVoice(0, VOICE_STEREO_16BIT, AUDIO_RATE, 0, buffers[pending], BYTES, volume, volume, NULL);
        pending = -1;
        segment = FRAMES;
        voice_started = 1;
        // ASND turns down AddVoice until the DSP has taken the SetVoice.
        return -1;
    }
    if (pending >= 0 && ASND_TestVoiceBufferReady(0) == 1) {
        if (ASND_AddVoice(0, buffers[pending], BYTES) == SND_OK) {
            segment += FRAMES;
            pending = -1;
        } else {
            status.refusals++;
        }
    }
    if (pending >= 0) return -1;
    for (int i = 0; i < BUFFERS; i++)
        if (ASND_TestPointer(0, buffers[i]) == 0) return i;
    return -1;
}

static void *producer(void *arg) {
    (void)arg;
    int first = 1;
    while (running) {
        LWP_MutexLock(lock);
        int target = paused ? -1 : first ? 0 : service();
        LWP_MutexUnlock(lock);
        if (target >= 0) {
            fill(buffers[target]);
            LWP_MutexLock(lock);
            pending = target;
            if (first) service();
            first = 0;
            LWP_MutexUnlock(lock);
        }
        usleep(1000);
    }
    return NULL;
}

int audio_start(Player *p) {
    audio_stop();
    LWP_MutexLock(lock);
    player = p;
    voice_started = 0;
    pending = -1;
    completed = segment = 0;
    paused = 0;
    memset(&status, 0, sizeof status);
    LWP_MutexUnlock(lock);
    running = 1;
    // Below the main thread (64), so a mix never delays a frame; the three buffers hold 128 ms of slack.
    if (LWP_CreateThread(&thread, producer, NULL, stack, sizeof stack, 60) != 0) {
        running = 0;
        return -1;
    }
    started_thread = 1;
    return 0;
}

void audio_stop(void) {
    if (started_thread) {
        running = 0;
        LWP_JoinThread(thread, NULL);
        started_thread = 0;
    }
    ASND_StopVoice(0);
    player = NULL;
}

uint64_t audio_played(void) {
    LWP_MutexLock(lock);
    uint64_t frames = completed;
    if (voice_started) {
        u32 ticks = ASND_GetTickCounterVoice(0);
        // The DSP's counter includes the 1024-sample block it is mixing right now.
        uint64_t in = ticks > 1024 ? (uint64_t)(ticks - 1024) * AUDIO_RATE / ASND_GetAudioRate() : 0;
        frames += in < segment ? in : segment;
    }
    LWP_MutexUnlock(lock);
    return frames;
}

void audio_pause(int on) {
    LWP_MutexLock(lock);
    paused = on;
    ASND_PauseVoice(0, on);
    LWP_MutexUnlock(lock);
}

// Chimes play on their own voices, so they never wait for the music stream.
#define CHIME_FRAMES (AUDIO_RATE * 2 / 5)
static int16_t *chimes;

void audio_chimes_prepare(void) {
    float *temp = malloc(sizeof(float) * CHIME_FRAMES);
    chimes = memalign(32, sizeof(int16_t) * CHIME_FRAMES * CHIME_COUNT);
    if (!temp || !chimes) {
        free(temp);
        free(chimes);
        chimes = NULL;
        return;
    }
    for (int c = 0; c < CHIME_COUNT; c++) {
        keyboard_chime(c, temp, CHIME_FRAMES);
        for (int i = 0; i < CHIME_FRAMES; i++) {
            float v = temp[i] < -1 ? -1 : temp[i] > 1 ? 1 : temp[i];
            chimes[c * CHIME_FRAMES + i] = (int16_t)(v * 30000);
        }
    }
    free(temp);
    DCFlushRange(chimes, sizeof(int16_t) * CHIME_FRAMES * CHIME_COUNT);
}

void audio_chime(int which) {
    if (which < 0 || which >= CHIME_COUNT || !chimes) return;
    static int next;
    int voice = 1 + next++ % 3;
    ASND_StopVoice(voice);
    ASND_SetVoice(voice, VOICE_MONO_16BIT, AUDIO_RATE, 0, chimes + which * CHIME_FRAMES, CHIME_FRAMES * 2, volume * 3 / 4,
                  volume * 3 / 4, NULL);
}

void audio_volume(float level) {
    // Full volume leaves headroom on the DSP's mixer, where the old fixed level sat.
    volume = (int)(level * 220);
    if (volume < 0) volume = 0;
    if (volume > 255) volume = 255;
    LWP_MutexLock(lock);
    if (voice_started) ASND_ChangeVolumeVoice(0, volume, volume);
    LWP_MutexUnlock(lock);
}

void audio_status(AudioStatus *out) {
    LWP_MutexLock(lock);
    *out = status;
    status.load_max = 0;
    LWP_MutexUnlock(lock);
}
