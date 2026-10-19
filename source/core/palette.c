// SPDX-License-Identifier: GPL-3.0-only
#include "palette.h"

#include "mathx.h"

#define H(x) {(uint8_t)((x) >> 16), (uint8_t)((x) >> 8), (uint8_t)(x)}

const Palette palettes[] = {
    {"midnight amber", H(0x07060f), {H(0x2a1b3d), H(0x5a2d5e), H(0xa8436f), H(0xe8735e), H(0xffb468), H(0xffe7b8)},
     {H(0xff6a8a), H(0xffd08a), H(0x8ad7ff)}, 0},
    {"phosphor", H(0x020a06), {H(0x0b2a17), H(0x145a2c), H(0x1f8f45), H(0x3ccf6e), H(0x8ef5a8), H(0xe1ffe9)},
     {H(0xb8ff6a), H(0xffffff), H(0x4fd0a0)}, 0},
    {"outrun", H(0x0b0420), {H(0x2b0b4d), H(0x5e1a8a), H(0xb02aa8), H(0xff4fa0), H(0xff9e6a), H(0xffe08a)},
     {H(0x3df2ff), H(0xff3d81), H(0xfff1c4)}, 0},
    {"ice", H(0x050b14), {H(0x0d2238), H(0x17405f), H(0x2a6f96), H(0x58a8cc), H(0xa6dcef), H(0xeffaff)},
     {H(0x9ad1ff), H(0xffffff), H(0xc5b3ff)}, 0},
    {"ember", H(0x0d0503), {H(0x2c0d07), H(0x63190b), H(0xa33212), H(0xe2621c), H(0xffa443), H(0xffe39a)},
     {H(0xfff1d6), H(0xff5a3c), H(0xffcd6b)}, 0},
    {"aurora", H(0x030812), {H(0x0a2233), H(0x0f4a4a), H(0x1f8a6a), H(0x4ccf8a), H(0xa6f0a0), H(0xe8ffe0)},
     {H(0xc38cff), H(0x6fe3ff), H(0xffffff)}, 0},
    {"sakura", H(0x120810), {H(0x3a1430), H(0x6e2850), H(0xb44a7a), H(0xf07fa5), H(0xffbfd2), H(0xfff0f4)},
     {H(0xffe3a0), H(0x9ad8ff), H(0xffffff)}, 0},
    {"commodore", H(0x2b2163), {H(0x403590), H(0x6c5eb5), H(0x8b7fd1), H(0xa9a0e6), H(0xcfc8f7), H(0xffffff)},
     {H(0xb8c76f), H(0xe09a6c), H(0x70a4b2)}, 0},
    {"dusk tape", H(0x14100e), {H(0x2b221c), H(0x4f3b2e), H(0x7c5b44), H(0xb8865c), H(0xe2b88a), H(0xfbe6c8)},
     {H(0xf2a48c), H(0x9cc4b8), H(0xffd68a)}, 0},
    {"snow", H(0xe9eef5), {H(0xcfd8e6), H(0x9fb0cc), H(0x6c82a8), H(0x445b85), H(0x24375e), H(0x0f1b36)},
     {H(0xd23f5d), H(0x2f7fd1), H(0xe0a42a)}, 1},
    {"deep sea", H(0x01060c), {H(0x062031), H(0x0a3b54), H(0x0f6a84), H(0x22a2b0), H(0x6fd8d0), H(0xd4fff6)},
     {H(0xffd38a), H(0x8ab4ff), H(0xffffff)}, 0},
    {"violet storm", H(0x08051a), {H(0x1d1240), H(0x3a2177), H(0x6236b3), H(0x9a5fe6), H(0xc9a3ff), H(0xf1e6ff)},
     {H(0xffdf6a), H(0x6af0ff), H(0xff7ab8)}, 0},
};
const int palette_count = (int)(sizeof palettes / sizeof palettes[0]);

enum { AMBER, PHOSPHOR, OUTRUN, ICE, EMBER, AURORA, SAKURA, COMMODORE, DUSK, SNOW, DEEP, STORM, COUNT };

static const int WEIGHTS[MOOD_COUNT][COUNT] = {
    [MOOD_SYNTHWAVE] = {[OUTRUN] = 40, [AMBER] = 25, [STORM] = 20, [EMBER] = 15},
    [MOOD_KEYGEN] = {[COMMODORE] = 30, [PHOSPHOR] = 25, [OUTRUN] = 15, [ICE] = 15, [STORM] = 15},
    [MOOD_AMBIENT] = {[ICE] = 25, [AURORA] = 25, [DEEP] = 25, [SAKURA] = 15, [SNOW] = 10},
    [MOOD_LOFI] = {[DUSK] = 40, [AMBER] = 25, [SAKURA] = 20, [DEEP] = 15},
    [MOOD_TRANCE] = {[STORM] = 30, [ICE] = 25, [OUTRUN] = 20, [AURORA] = 25},
    [MOOD_JUNGLE] = {[PHOSPHOR] = 30, [EMBER] = 25, [DEEP] = 25, [STORM] = 20},
    [MOOD_MUSICBOX] = {[SAKURA] = 30, [SNOW] = 25, [ICE] = 25, [AURORA] = 20},
    [MOOD_DUB] = {[DEEP] = 35, [PHOSPHOR] = 20, [ICE] = 25, [DUSK] = 20},
};

Rgbf palette_ramp(const Palette *p, float v) {
    v = saturate(v) * (RAMP_STEPS - 1);
    int i = mfloor_i(v);
    if (i >= RAMP_STEPS - 1) return rgbf(p->ramp[RAMP_STEPS - 1]);
    return rgbf_mix(rgbf(p->ramp[i]), rgbf(p->ramp[i + 1]), v - (float)i);
}

Rgbf palette_accent(const Palette *p, int i) {
    return rgbf(p->accent[((i % 3) + 3) % 3]);
}

const Palette *palette_pick(Rng *r, Mood mood) {
    int m = mood >= 0 && mood < MOOD_COUNT ? mood : MOOD_SYNTHWAVE;
    return &palettes[rng_weighted(r, WEIGHTS[m], COUNT)];
}
