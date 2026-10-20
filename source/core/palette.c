// SPDX-License-Identifier: GPL-3.0-only
#include "palette.h"

#include "mathx.h"

#include <stdio.h>
#include <string.h>

#define H(x) {(uint8_t)((x) >> 16), (uint8_t)((x) >> 8), (uint8_t)(x)}

enum {
    KEYGEN = 1 << MOOD_KEYGEN,
    AMBIENT = 1 << MOOD_AMBIENT,
    SYNTHWAVE = 1 << MOOD_SYNTHWAVE,
    LOFI = 1 << MOOD_LOFI,
    TRANCE = 1 << MOOD_TRANCE,
    EIGHT_BIT = 1 << MOOD_8BIT,
    MUSICBOX = 1 << MOOD_MUSICBOX,
    DUB = 1 << MOOD_DUB,
    DUBSTEP = 1 << MOOD_DUBSTEP,
    SIXTEEN_BIT = 1 << MOOD_16BIT,
    EVERY = (1 << MOOD_COUNT) - 1
};

enum { RAINBOW = 2 };

typedef struct {
    const char *name;
    Rgb bg, ramp[RAMP_STEPS], accent[3];
    int light;
    // The moods it suits best; any mood can still draw it now and then.
    unsigned moods;
    // Multi-hued palettes keep their hues: turning them would only muddle them. RAINBOW also paints by place.
    int fixed;
} Curated;

static const Curated CURATED[] = {
    {"midnight amber", H(0x07060f), {H(0x2a1b3d), H(0x5a2d5e), H(0xa8436f), H(0xe8735e), H(0xffb468), H(0xffe7b8)},
     {H(0xff6a8a), H(0xffd08a), H(0x8ad7ff)}, 0, SYNTHWAVE | LOFI | DUB, 0},
    {"phosphor", H(0x020a06), {H(0x0b2a17), H(0x145a2c), H(0x1f8f45), H(0x3ccf6e), H(0x8ef5a8), H(0xe1ffe9)},
     {H(0xb8ff6a), H(0xffffff), H(0x4fd0a0)}, 0, KEYGEN | DUB | DUBSTEP | EIGHT_BIT, 0},
    {"outrun", H(0x0b0420), {H(0x2b0b4d), H(0x5e1a8a), H(0xb02aa8), H(0xff4fa0), H(0xff9e6a), H(0xffe08a)},
     {H(0x3df2ff), H(0xff3d81), H(0xfff1c4)}, 0, SYNTHWAVE | TRANCE | KEYGEN | DUBSTEP, 0},
    {"ice", H(0x050b14), {H(0x0d2238), H(0x17405f), H(0x2a6f96), H(0x58a8cc), H(0xa6dcef), H(0xeffaff)},
     {H(0x9ad1ff), H(0xffffff), H(0xc5b3ff)}, 0, AMBIENT | TRANCE | MUSICBOX | DUB | SIXTEEN_BIT, 0},
    {"ember", H(0x0d0503), {H(0x2c0d07), H(0x63190b), H(0xa33212), H(0xe2621c), H(0xffa443), H(0xffe39a)},
     {H(0xfff1d6), H(0xff5a3c), H(0xffcd6b)}, 0, SYNTHWAVE | DUBSTEP, 0},
    {"northern lights", H(0x030812), {H(0x0a2233), H(0x0f4a4a), H(0x1f8a6a), H(0x4ccf8a), H(0xa6f0a0), H(0xe8ffe0)},
     {H(0xc38cff), H(0x6fe3ff), H(0xffffff)}, 0, AMBIENT | TRANCE | MUSICBOX, 0},
    {"sakura", H(0x120810), {H(0x3a1430), H(0x6e2850), H(0xb44a7a), H(0xf07fa5), H(0xffbfd2), H(0xfff0f4)},
     {H(0xffe3a0), H(0x9ad8ff), H(0xffffff)}, 0, AMBIENT | LOFI | MUSICBOX | SIXTEEN_BIT, 0},
    {"commodore", H(0x2b2163), {H(0x403590), H(0x6c5eb5), H(0x8b7fd1), H(0xa9a0e6), H(0xcfc8f7), H(0xffffff)},
     {H(0xb8c76f), H(0xe09a6c), H(0x70a4b2)}, 0, KEYGEN | EIGHT_BIT, 1},
    {"dusk tape", H(0x14100e), {H(0x2b221c), H(0x4f3b2e), H(0x7c5b44), H(0xb8865c), H(0xe2b88a), H(0xfbe6c8)},
     {H(0xf2a48c), H(0x9cc4b8), H(0xffd68a)}, 0, LOFI | DUB, 0},
    {"porcelain", H(0xe9eef5), {H(0xcfd8e6), H(0x9fb0cc), H(0x6c82a8), H(0x445b85), H(0x24375e), H(0x0f1b36)},
     {H(0xd23f5d), H(0x2f7fd1), H(0xe0a42a)}, 1, AMBIENT | MUSICBOX, 0},
    {"deep sea", H(0x01060c), {H(0x062031), H(0x0a3b54), H(0x0f6a84), H(0x22a2b0), H(0x6fd8d0), H(0xd4fff6)},
     {H(0xffd38a), H(0x8ab4ff), H(0xffffff)}, 0, AMBIENT | DUB | SIXTEEN_BIT, 0},
    {"violet storm", H(0x08051a), {H(0x1d1240), H(0x3a2177), H(0x6236b3), H(0x9a5fe6), H(0xc9a3ff), H(0xf1e6ff)},
     {H(0xffdf6a), H(0x6af0ff), H(0xff7ab8)}, 0, TRANCE | SYNTHWAVE | DUBSTEP | KEYGEN, 0},
    {"rainbow", H(0x000000), {H(0x2a2a2a), H(0x505050), H(0x7c7c7c), H(0xa8a8a8), H(0xd6d6d6), H(0xffffff)},
     {H(0xffffff), H(0xffffff), H(0xd6d6d6)}, 0, EVERY, RAINBOW},
    {"cmyk", H(0x09090c), {H(0x1c2530), H(0x006f7d), H(0x00b4e6), H(0xe6007e), H(0xffd400), H(0xfff6d8)},
     {H(0x00b4e6), H(0xe6007e), H(0xffd400)}, 0, EVERY, 1},
    {"vaporwave", H(0x1a0b2e), {H(0x3d1a5c), H(0x7a2c8f), H(0xc74ab0), H(0xff7ac6), H(0x8ef0ff), H(0xf2fbff)},
     {H(0x00f0ff), H(0xff9de2), H(0xfffd82)}, 0, SYNTHWAVE | LOFI | TRANCE, 1},
    {"pocket green", H(0x0f1a0f), {H(0x1f3a1f), H(0x306230), H(0x4f8a2f), H(0x8bac0f), H(0xb6d23c), H(0xe0f8d0)},
     {H(0x9bbc0f), H(0xe0f8d0), H(0x8bac0f)}, 0, KEYGEN | EIGHT_BIT, 1},
    {"noir", H(0x0a0a0a), {H(0x1e1e1e), H(0x3a3a3a), H(0x666666), H(0x9a9a9a), H(0xcfcfcf), H(0xffffff)},
     {H(0xe3242b), H(0xffffff), H(0x8a8a8a)}, 0, DUB | DUBSTEP | LOFI, 1},
    {"forest", H(0x06100a), {H(0x0f2a18), H(0x1e4d2b), H(0x2f7a3a), H(0x5aa84a), H(0xa8d36a), H(0xeef7c8)},
     {H(0xffd166), H(0xff8fab), H(0x9fe2ff)}, 0, AMBIENT | LOFI | MUSICBOX | SIXTEEN_BIT, 0},
    {"sunrise", H(0x1a0f1f), {H(0x4a1d3f), H(0x8a2f4f), H(0xd9534f), H(0xf28c54), H(0xffc56b), H(0xfff2c4)},
     {H(0x6ee7ff), H(0xffffff), H(0xff9ac1)}, 0, SYNTHWAVE | TRANCE | LOFI | MUSICBOX | SIXTEEN_BIT, 0},
    {"mint", H(0x04120f), {H(0x0b2e28), H(0x12584b), H(0x1c8a74), H(0x3ec9a7), H(0x9ef0d5), H(0xeffff9)},
     {H(0xff9ac1), H(0xffe28a), H(0xffffff)}, 0, AMBIENT | TRANCE | MUSICBOX | KEYGEN, 0},
    {"lavender", H(0x120d1f), {H(0x2b2147), H(0x4b3a7a), H(0x7a63b8), H(0xa993e6), H(0xd2c6ff), H(0xf6f2ff)},
     {H(0xffd1e8), H(0xb8f2e6), H(0xfff3b0)}, 0, AMBIENT | MUSICBOX | LOFI, 0},
    {"verdigris", H(0x0b1110), {H(0x1c2b28), H(0x2f4d47), H(0x3f7a6e), H(0xb0683f), H(0xe39a5c), H(0xf7d9b5)},
     {H(0x6fe3cf), H(0xffb37a), H(0xfff1df)}, 0, DUB | LOFI, 1},
    {"blood moon", H(0x120406), {H(0x2f080d), H(0x5c0f18), H(0x93172a), H(0xd1344a), H(0xff7a6b), H(0xffd2c4)},
     {H(0xffb000), H(0xffe9d6), H(0xff3d55)}, 0, DUBSTEP | SYNTHWAVE, 0},
    {"citrus", H(0x0f1205), {H(0x263010), H(0x4f5f12), H(0x8aa61a), H(0xd9d92b), H(0xffb627), H(0xfff5c2)},
     {H(0xff5e5b), H(0x7bdff2), H(0xffffff)}, 0, KEYGEN | TRANCE | EIGHT_BIT, 0},
    {"ultraviolet", H(0x0a0518), {H(0x1d0d45), H(0x3b1a8f), H(0x6a2be0), H(0xa855ff), H(0xe08cff), H(0xfbe8ff)},
     {H(0x39ff14), H(0x00f5ff), H(0xffffff)}, 0, TRANCE | DUBSTEP | KEYGEN | SYNTHWAVE, 0},
    {"matcha", H(0x0e110a), {H(0x22291a), H(0x3d4a2a), H(0x66803f), H(0x99b35c), H(0xcfe0a0), H(0xf7fbe8)},
     {H(0xf2a7b5), H(0xf5e6c8), H(0xffffff)}, 0, LOFI | AMBIENT | MUSICBOX, 0},
    {"neon tokyo", H(0x08060f), {H(0x1f0f3a), H(0x4b1078), H(0xc2187a), H(0xff4f9a), H(0xffb0d8), H(0xffffff)},
     {H(0x00e7ff), H(0xf9f871), H(0x7cff6b)}, 0, SYNTHWAVE | DUBSTEP | KEYGEN | TRANCE, 1},
    {"autumn", H(0x110a06), {H(0x2b160c), H(0x5a2a12), H(0x96481a), H(0xd77a2b), H(0xf2b35a), H(0xfde7c0)},
     {H(0xb5d99c), H(0xffd6a5), H(0xc76b98)}, 0, LOFI | MUSICBOX | DUB | SIXTEEN_BIT, 0},
    {"sherbet", H(0x140a12), {H(0x3a1a33), H(0x6b2d57), H(0xb84a7a), H(0xff8fa3), H(0xffd6a5), H(0xfffbe0)},
     {H(0xa0e7e5), H(0xb4f8c8), H(0xfbe7c6)}, 0, LOFI | MUSICBOX | SYNTHWAVE | EIGHT_BIT, 1},
    {"teal and orange", H(0x050b10), {H(0x0c2230), H(0x154a5e), H(0x1f7f8c), H(0x3fb8b0), H(0xf2a65a), H(0xfff1dc)},
     {H(0xff6b35), H(0xffffff), H(0x8ce1ff)}, 0, TRANCE | DUB | DUBSTEP | AMBIENT, 1},
    {"ocean sunset", H(0x0b0d1f), {H(0x1d1b45), H(0x3c2a6e), H(0xa2416b), H(0xe9765b), H(0xf9c46b), H(0xfff2cf)},
     {H(0x5ee2ff), H(0xffffff), H(0xffa7c4)}, 0, SYNTHWAVE | AMBIENT | LOFI, 1},
    {"acid", H(0x050605), {H(0x1a2408), H(0x3a5410), H(0x6b9a12), H(0xb5e61d), H(0xe8ff5a), H(0xfaffd0)},
     {H(0xff2fd0), H(0x2ff3ff), H(0xffffff)}, 0, KEYGEN | TRANCE | DUBSTEP, 1},
    {"candy", H(0x170a14), {H(0x3b1530), H(0x6e2357), H(0xb33a84), H(0xf065b0), H(0xff9fd2), H(0xffe3f2)},
     {H(0x7ff5c8), H(0xfff27a), H(0x8fd3ff)}, 0, MUSICBOX | LOFI | SYNTHWAVE | EIGHT_BIT, 1},
    {"gold leaf", H(0x0a0805), {H(0x241a0b), H(0x4d3812), H(0x8a6420), H(0xc9973a), H(0xf0c96a), H(0xfff2c8)},
     {H(0xfff8e0), H(0xd4a24a), H(0x9c7cff)}, 0, AMBIENT | MUSICBOX | TRANCE | DUB | SIXTEEN_BIT, 0},
    {"infrared", H(0x0a0206), {H(0x2a0516), H(0x5c0a2a), H(0x9e1238), H(0xe8352f), H(0xff8a2a), H(0xffe38a)},
     {H(0x7a5cff), H(0xffffff), H(0x2ad4ff)}, 0, DUBSTEP | KEYGEN | TRANCE, 1},
    {"cobalt", H(0x02040c), {H(0x0a1430), H(0x142a5e), H(0x23479a), H(0x4a78d6), H(0x9db8f5), H(0xeef3ff)},
     {H(0xffffff), H(0xffd98a), H(0x7af0e0)}, 0, AMBIENT | TRANCE | DUB | MUSICBOX | SIXTEEN_BIT, 0},
    {"rose gold", H(0x140c0c), {H(0x2e1a1a), H(0x553030), H(0x8c5048), H(0xc77f6e), H(0xeab59f), H(0xfff0e8)},
     {H(0xffd6c2), H(0x9ad0c2), H(0xf5c86a)}, 0, LOFI | MUSICBOX | AMBIENT, 0},
    {"toxic", H(0x07020c), {H(0x1a0a29), H(0x3a1257), H(0x5e1d8a), H(0x2fbf4a), H(0x6dff5c), H(0xe3ffd6)},
     {H(0xc84bff), H(0xf7ff00), H(0xffffff)}, 0, DUBSTEP | KEYGEN, 1},
    {"arcade", H(0x000000), {H(0x101018), H(0x252540), H(0x3a3aa0), H(0x5f5fff), H(0xa5a5ff), H(0xffffff)},
     {H(0xff2a2a), H(0xffe600), H(0x2aff6a)}, 0, KEYGEN | SYNTHWAVE | DUBSTEP | EIGHT_BIT, 1},
    {"paper", H(0xf3ecdc), {H(0xe2d6bc), H(0xc2ad86), H(0x8f7552), H(0x5e4a33), H(0x33281c), H(0x15100a)},
     {H(0xc23b22), H(0x2b5f8a), H(0x3e7d4f)}, 1, MUSICBOX | LOFI, 0},
    {"cyberpunk", H(0x0a0a06), {H(0x26260c), H(0x4d4a12), H(0x8a8418), H(0xd6cc1e), H(0xfff04a), H(0xfffbd0)},
     {H(0x00f0ff), H(0xff2a6d), H(0xffffff)}, 0, DUBSTEP | KEYGEN | SYNTHWAVE | TRANCE, 1},
    {"peacock", H(0x0a0010), {H(0x2a0a40), H(0x440154), H(0x3b528b), H(0x21918c), H(0x5ec962), H(0xfde725)},
     {H(0xfde725), H(0x21918c), H(0xffffff)}, 0, AMBIENT | DUB | TRANCE | KEYGEN | SIXTEEN_BIT, 1},
    {"strawberry", H(0x14050a), {H(0x3a0c1a), H(0x6e1430), H(0xb0224a), H(0xec4c6a), H(0xff9fb0), H(0xfff0f2)},
     {H(0x7ee08a), H(0xfff2c4), H(0xffffff)}, 0, MUSICBOX | LOFI | SYNTHWAVE | EIGHT_BIT, 0},
    {"starry night", H(0x050a1e), {H(0x0f1d4a), H(0x1f3a7a), H(0x3a5fb0), H(0xf2b81d), H(0xffd84a), H(0xfff5c8)},
     {H(0xff7a2a), H(0x8ad4ff), H(0xffffff)}, 0, LOFI | MUSICBOX | AMBIENT | SYNTHWAVE | SIXTEEN_BIT, 1},
    {"kanagawa", H(0x0d1220), {H(0x1c2440), H(0x2d3a63), H(0x4a5d8c), H(0x7f93b8), H(0xc9cbb8), H(0xf2ecd8)},
     {H(0xc34043), H(0xdca561), H(0x7aa89f)}, 0, AMBIENT | LOFI | MUSICBOX | DUB | SIXTEEN_BIT, 0},
    {"inferno", H(0x000004), {H(0x280b54), H(0x65156e), H(0x9f2a63), H(0xd44842), H(0xf98e09), H(0xfcffa4)},
     {H(0xfcffa4), H(0x65156e), H(0xf98e09)}, 0, DUBSTEP | TRANCE | KEYGEN | AMBIENT, 1},
    {"plum", H(0x120812), {H(0x2c1230), H(0x4f1f52), H(0x7d3478), H(0xb35e9a), H(0xe8a3c4), H(0xfde6ef)},
     {H(0xffc8a2), H(0xa8d5ba), H(0xfff4d6)}, 0, LOFI | DUB | AMBIENT, 0},
    {"blueprint", H(0x0a2a5e), {H(0x153a75), H(0x2350a0), H(0x3d6fc4), H(0x7da1e0), H(0xbcd3f5), H(0xffffff)},
     {H(0xffffff), H(0x9ff0ff), H(0xffe680)}, 0, KEYGEN | SYNTHWAVE | TRANCE | EIGHT_BIT, 1},
    {"halloween", H(0x0a0608), {H(0x2a1208), H(0x55230c), H(0x8f3a0e), H(0xd65f12), H(0xff9a2e), H(0xffd6a0)},
     {H(0x9b4dff), H(0x7dff6a), H(0xfff2e0)}, 0, SYNTHWAVE | DUBSTEP | KEYGEN | LOFI, 1},
    {"cga", H(0x000000), {H(0x1a1a1a), H(0x3a3a3a), H(0x6a6a6a), H(0xaaaaaa), H(0xd5d5d5), H(0xffffff)},
     {H(0x55ffff), H(0xff55ff), H(0xffffff)}, 0, KEYGEN | DUBSTEP | TRANCE | EIGHT_BIT, 1},
    {"mesa", H(0x140b08), {H(0x34180e), H(0x5e2a15), H(0x91431f), H(0xc76a32), H(0xe8a05e), H(0xf8dcb0)},
     {H(0x7fb8c9), H(0xa8c48a), H(0xfff1d8)}, 0, AMBIENT | DUB | LOFI | SYNTHWAVE, 0},
    {"mint chip", H(0x0e0a08), {H(0x2a1c14), H(0x4a3022), H(0x6e4a33), H(0x9be0c0), H(0xc8f5e2), H(0xf2fffa)},
     {H(0xfff2e0), H(0x7ff0c4), H(0xd9a07a)}, 0, LOFI | MUSICBOX, 1},
    {"foundry", H(0x0c0d10), {H(0x1e2229), H(0x343b46), H(0x55606f), H(0x8392a3), H(0xc0cbd6), H(0xf2f5f8)},
     {H(0xff7a1a), H(0xffc04a), H(0xff3d2e)}, 0, DUBSTEP | KEYGEN | DUB | TRANCE, 1},
    {"twilight", H(0x0a0b1e), {H(0x1c1d45), H(0x34306e), H(0x5a3f8f), H(0x9356a6), H(0xd287b5), H(0xffd3d8)},
     {H(0xffe9a8), H(0x9fe3ff), H(0xffffff)}, 0, AMBIENT | MUSICBOX | LOFI | DUB | SIXTEEN_BIT, 0},
    // An old home computer's colors in the order of their brightness, as its manual listed them.
    {"home micro", H(0x000000), {H(0x00006e), H(0x0000d7), H(0xd700d7), H(0x00d700), H(0x00d7d7), H(0xffff8a)},
     {H(0xffffff), H(0xff57ff), H(0x57ffff)}, 0, KEYGEN | DUBSTEP | EIGHT_BIT, 1},
    {"risograph", H(0xf4efe3), {H(0xe6ddd0), H(0xc8cfe3), H(0x8ea5d8), H(0x5a74c8), H(0x6a40a6), H(0x2a2160)},
     {H(0xff48b0), H(0xffcc00), H(0x00a7a0)}, 1, LOFI | MUSICBOX | AMBIENT, 1},
    {"goldfish", H(0x03111a), {H(0x072033), H(0x0c3a57), H(0x14587e), H(0xe8642a), H(0xff9446), H(0xfff4e8)},
     {H(0xffffff), H(0xf2543d), H(0x7fd6ff)}, 0, AMBIENT | LOFI | DUB | MUSICBOX | SIXTEEN_BIT, 1},
    {"jade", H(0x03100c), {H(0x0a2a1f), H(0x0f4636), H(0x176b52), H(0x2a9c78), H(0x77d0a8), H(0xe6f8ec)},
     {H(0xffc94a), H(0xfff3d6), H(0xff8fa3)}, 0, AMBIENT | DUB | LOFI | TRANCE | SIXTEEN_BIT, 0},
    {"amber terminal", H(0x0a0600), {H(0x2b1600), H(0x573000), H(0x8a4f00), H(0xc77a00), H(0xffab2e), H(0xffe3ad)},
     {H(0xffc457), H(0xfff2d9), H(0xff8a1c)}, 0, KEYGEN | DUB | DUBSTEP | EIGHT_BIT, 0},
    {"seafoam", H(0xe6f5ef), {H(0xcde8df), H(0x9ccfc0), H(0x5fae9c), H(0x2f8578), H(0x1b5a5a), H(0x0d2e38)},
     {H(0xff6f59), H(0xf2b134), H(0xe85d9a)}, 1, AMBIENT | LOFI | MUSICBOX | DUB, 0},
};
#define CURATED_COUNT ((int)(sizeof CURATED / sizeof CURATED[0]))

Rgbf palette_ramp(const Palette *p, float v) {
    v = saturate(v) * (RAMP_STEPS - 1);
    int i = mfloor_i(v);
    if (i >= RAMP_STEPS - 1) return rgbf(p->ramp[RAMP_STEPS - 1]);
    return rgbf_mix(rgbf(p->ramp[i]), rgbf(p->ramp[i + 1]), v - (float)i);
}

Rgbf palette_accent(const Palette *p, int i) {
    return rgbf(p->accent[((i % 3) + 3) % 3]);
}

static void from_curated(const Curated *c, Palette *out) {
    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s", c->name);
    out->bg = c->bg;
    memcpy(out->ramp, c->ramp, sizeof out->ramp);
    memcpy(out->accent, c->accent, sizeof out->accent);
    out->light = c->light;
    out->rainbow = c->fixed == RAINBOW;
}

// Bright, full stops, with blue and violet lifted so they read on black as well as yellow does.
static const Rgb SPECTRUM[] = {H(0xff3b4e), H(0xff8a1f), H(0xffe03a), H(0x4cf06a), H(0x2bd6ff), H(0x5070ff), H(0xb455ff)};

Rgbf palette_rainbow_at(const Grid *g, int col, int row, float t) {
    const int n = (int)(sizeof SPECTRUM / sizeof SPECTRUM[0]);
    // Bands run corner to corner, so the whole spectrum is on screen at once, and drift a full turn every 20 s.
    float u = mfract((float)col / (float)g->cols + 0.35f * (float)row / (float)g->rows + t * 0.05f) * (float)n;
    int i = (int)u;
    return rgbf_mix(rgbf(SPECTRUM[i % n]), rgbf(SPECTRUM[(i + 1) % n]), u - (float)i);
}

void palette_paint_rainbow(Grid *g, float t) {
    for (int row = 0; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            Cell *c = grid_at(g, col, row);
            if (c->glyph == ' ') continue;
            int v = c->fg.r > c->fg.g ? c->fg.r : c->fg.g;
            v = v > c->fg.b ? v : c->fg.b;
            // Lifted, so the dim steps a gentle scene lives in still read as color on black.
            c->fg = rgb_from_f(rgbf_scale(palette_rainbow_at(g, col, row, t), msqrt((float)v / 255.0f)));
        }
}

int palette_curated_count(void) {
    return CURATED_COUNT;
}

void palette_curated(int index, Palette *out) {
    from_curated(&CURATED[((index % CURATED_COUNT) + CURATED_COUNT) % CURATED_COUNT], out);
}

// Turns a color around the gray axis, which keeps a palette's colors in the same relation to each other.
static Rgb drift(Rgb c, float degrees) {
    float a = degrees * PI_F / 180.0f, c_a = mcos(a), s_a = msin(a);
    float k = (1 - c_a) / 3, s = s_a * 0.57735f, r = c.r, g = c.g, b = c.b;
    Rgbf out = {(c_a + k) * r + (k - s) * g + (k + s) * b, (k + s) * r + (c_a + k) * g + (k - s) * b,
                (k - s) * r + (k + s) * g + (c_a + k) * b};
    return rgb_from_f(out);
}

static void turn(Palette *p, float degrees) {
    p->bg = drift(p->bg, degrees);
    for (int i = 0; i < RAMP_STEPS; i++) p->ramp[i] = drift(p->ramp[i], degrees);
    for (int i = 0; i < 3; i++) p->accent[i] = drift(p->accent[i], degrees);
}

/* ---------- Generated palettes, built in OKLCH so their ramps brighten evenly ---------- */

static float encode_srgb(float x) {
    if (x <= 0) return 0;
    if (x >= 1) return 255;
    return 255 * (x <= 0.0031308f ? 12.92f * x : 1.055f * mexp2(mlog2(x) / 2.4f) - 0.055f);
}

static Rgb oklch(float lightness, float chroma, float hue) {
    float a = chroma * mcos(hue * PI_F / 180.0f), b = chroma * msin(hue * PI_F / 180.0f);
    float l = lightness + 0.3963377774f * a + 0.2158037573f * b, m = lightness - 0.1055613458f * a - 0.0638541728f * b;
    float s = lightness - 0.0894841775f * a - 1.2914855480f * b;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    Rgbf c = {encode_srgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s),
              encode_srgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s),
              encode_srgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s)};
    return rgb_from_f(c);
}

typedef struct {
    float hue, sweep, chroma;
    int scheme;
} Recipe;

static Recipe recipe(Rng *r) {
    return (Recipe){rng_between(r, 0, 360), rng_between(r, -90, 90), rng_between(r, 0.09f, 0.17f), rng_range(r, 0, 4)};
}

static void generate(Recipe k, Palette *out) {
    static const char *const hues[12] = {"rose", "coral", "amber", "gold", "lime", "jade",
                                         "teal", "azure", "cobalt", "indigo", "violet", "orchid"};
    static const char *const moods[4] = {"glow", "drift", "bloom", "haze"};
    float hue = mfract(k.hue / 360.0f) * 360.0f, sweep = k.sweep, chroma = k.chroma;
    int scheme = k.scheme;
    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s %s", hues[(int)(mfract((hue + 15) / 360.0f) * 12) % 12], moods[scheme]);
    out->bg = oklch(0.14f, 0.035f, hue);
    for (int i = 0; i < RAMP_STEPS; i++) {
        float t = (float)i / (RAMP_STEPS - 1);
        out->ramp[i] = oklch(0.27f + 0.68f * t, chroma * (0.45f + 0.75f * msin(t * PI_F)) * (1 - 0.6f * t * t), hue + sweep * t);
    }
    // Accents sit across the wheel: the complement, or a triad, or the two sides of a split.
    static const float offsets[4][3] = {{180, 150, 210}, {120, 240, 180}, {150, 210, 30}, {90, 180, 270}};
    for (int i = 0; i < 3; i++) out->accent[i] = oklch(0.8f, 0.14f, hue + offsets[scheme][i]);
}

static int pick_curated(Rng *r, Mood mood) {
    int weights[CURATED_COUNT];
    for (int i = 0; i < CURATED_COUNT; i++) {
        int suits = mood >= 0 && mood < MOOD_COUNT && (CURATED[i].moods & (1u << mood));
        weights[i] = CURATED[i].moods == EVERY ? 6 : suits ? 10 : 1;
    }
    return rng_weighted(r, weights, CURATED_COUNT);
}

static float swing(Rng *r, float lo, float hi) {
    return (rng_chance(r, 50) ? 1 : -1) * rng_between(r, lo, hi);
}

void palette_family(Rng *r, Mood mood, Palette out[PALETTE_FAMILY]) {
    int curated = rng_chance(r, 65), used[PALETTE_FAMILY] = {-1, -1, -1, -1};
    Recipe k = {0};
    if (curated) {
        used[0] = pick_curated(r, mood);
        from_curated(&CURATED[used[0]], &out[0]);
        if (!CURATED[used[0]].fixed) turn(&out[0], rng_between(r, -25, 25));
    } else {
        k = recipe(r);
        generate(k, &out[0]);
    }
    // Builds and breaks may lean a little off the base; every other place, and the drops always, take a palette of
    // their own: another one the mood suits that the piece isn't using yet, or one generated well round the wheel.
    for (int i = 1; i < PALETTE_FAMILY; i++) {
        int movable = !curated || !CURATED[used[0]].fixed;
        if (i == 2 && movable && rng_chance(r, 40)) {
            if (curated) {
                out[2] = out[0];
                turn(&out[2], swing(r, 15, 35));
            } else {
                Recipe lean = k;
                lean.hue += swing(r, 15, 35);
                generate(lean, &out[2]);
            }
            continue;
        }
        if (rng_chance(r, 70)) {
            int pick = pick_curated(r, mood);
            for (int tries = 0; tries < 12 && (pick == used[0] || pick == used[1] || pick == used[2] || pick == used[3]); tries++)
                pick = pick_curated(r, mood);
            used[i] = pick;
            from_curated(&CURATED[pick], &out[i]);
            if (!CURATED[pick].fixed) turn(&out[i], rng_between(r, -25, 25));
        } else {
            Recipe other = recipe(r);
            if (!curated) other.hue = k.hue + swing(r, 90, 180);
            generate(other, &out[i]);
        }
    }
}
