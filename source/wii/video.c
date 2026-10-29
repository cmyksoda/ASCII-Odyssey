// SPDX-License-Identifier: GPL-3.0-or-later
#include "video.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <ogc/conf.h>
#include <string.h>

#define FIFO_SIZE (1024 * 1024)
#define MAX_FACES 16

typedef struct {
    const FontFace *face;
    void *texels;
    GXTexObj object;
} FaceTexture;

static GXRModeObj *mode;
static void *xfb[2];
static int front, shown, wide;
static void *fifo;
static FaceTexture faces[MAX_FACES];
static int face_count;
static void *glow_texels;
static GXTexObj glow_tex;
static unsigned build_time, gpu_time;
static int interval = 1;
static u32 last_retrace;
static volatile u32 retraces;

static void on_retrace(u32 count) {
    retraces = count;
}

// A PAL console follows its own 50/60 Hz setting, as licensed games do; 50 Hz scales the 480-line EFB onto 576.
static GXRModeObj *pick_mode(void) {
    GXRModeObj *m = VIDEO_GetPreferredMode(NULL);
    if (m->xfbHeight > 480 && CONF_GetEuRGB60() > 0)
        m = CONF_GetProgressiveScan() > 0 && VIDEO_HaveComponentCable() ? &TVEurgb60Hz480Prog : &TVEurgb60Hz480IntDf;
    else if (m->xfbHeight > 480)
        m = &TVPal576IntDfScale;
    return m;
}

static void copy_setup(void) {
    GX_SetViewport(0, 0, mode->fbWidth, mode->efbHeight, 0, 1);
    u32 lines = GX_SetDispCopyYScale(GX_GetYScaleFactor(mode->efbHeight, mode->xfbHeight));
    GX_SetScissor(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopySrc(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopyDst(mode->fbWidth, lines);
    GX_SetCopyFilter(mode->aa, mode->sample_pattern, GX_TRUE, mode->vfilter);
    GX_SetFieldMode(mode->field_rendering, mode->viHeight == 2 * mode->xfbHeight ? GX_ENABLE : GX_DISABLE);
    GX_SetDispCopyGamma(GX_GM_1_0);
}

int video_init(void) {
    VIDEO_Init();
    mode = pick_mode();
    wide = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
    for (int i = 0; i < 2; i++) {
        void *fb = SYS_AllocateFramebuffer(mode);
        if (!fb) return -1;
        xfb[i] = MEM_K0_TO_K1(fb);
        VIDEO_ClearFrameBuffer(mode, xfb[i], COLOR_BLACK);
    }
    VIDEO_Configure(mode);
    VIDEO_SetPostRetraceCallback(on_retrace);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    fifo = memalign(32, FIFO_SIZE);
    if (!fifo) return -1;
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);
    copy_setup();
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    glow_texels = memalign(32, GX_GetTexBufferSize(FB_W / 2, FB_H / 2, GX_TF_RGB565, GX_FALSE, 0));
    if (glow_texels) {
        GX_InitTexObj(&glow_tex, glow_texels, FB_W / 2, FB_H / 2, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&glow_tex, GX_LINEAR, GX_LINEAR);
    }
    return 0;
}

int video_wide(void) {
    return wide;
}

// GX reads textures in 8x4 tiles; the baked atlases are padded to whole tiles.
static FaceTexture *face_texture(const FontFace *f) {
    for (int i = 0; i < face_count; i++)
        if (faces[i].face == f) return &faces[i];
    if (face_count == MAX_FACES) return NULL;
    FaceTexture *t = &faces[face_count];
    t->texels = memalign(32, (size_t)f->atlas_w * f->atlas_h);
    if (!t->texels) return NULL;
    unsigned char *out = t->texels;
    for (int ty = 0; ty < f->atlas_h; ty += 4)
        for (int tx = 0; tx < f->atlas_w; tx += 8)
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 8; x++) *out++ = f->atlas[(ty + y) * f->atlas_w + tx + x];
    DCFlushRange(t->texels, (u32)f->atlas_w * f->atlas_h);
    GX_InitTexObj(&t->object, t->texels, f->atlas_w, f->atlas_h, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&t->object, GX_NEAR, GX_NEAR);
    t->face = f;
    face_count++;
    GX_InvalidateTexAll();
    return t;
}

static void glyph_state(void) {
    Mtx44 projection;
    guOrtho(projection, 0, FB_H, 0, FB_W, 0, 1);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U16, 0);
    GX_InvVtxCache();
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    // Texture coordinates arrive in texels; this matrix scales them into the atlas.
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);
    GX_SetNumTevStages(1);
    GX_SetNumIndStages(0);
    GX_SetTevDirect(GX_TEVSTAGE0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    // Glyph atlases are coverage only: the color comes from the vertex, the alpha from the glyph.
    GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
    GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
    GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_SetDither(GX_FALSE);
}

static u8 channel(float v) {
    return v <= 0 ? 0 : v >= 255 ? 255 : (u8)(v + 0.5f);
}

static void draw_grid(const Grid *g, float gain, float flash, float shake_x, float shake_y) {
    FaceTexture *t = face_texture(g->face);
    if (!t) return;
    const FontFace *f = g->face;
    GX_LoadTexObj(&t->object, GX_TEXMAP0);
    Mtx m;
    guMtxScale(m, 1.0f / (float)f->atlas_w, 1.0f / (float)f->atlas_h, 1);
    GX_LoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
    // Whole pixels only, as on the PC: a pixel font on fractional rows smears.
    guMtxTrans(m, (float)(int)(shake_x >= 0 ? shake_x + 0.5f : shake_x - 0.5f),
               (float)(int)(shake_y >= 0 ? shake_y + 0.5f : shake_y - 0.5f), 0);
    GX_LoadPosMtxImm(m, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
    int count = 0;
    for (int i = 0; i < g->cols * g->rows; i++) count += g->cells[i].glyph > ' ';
    if (!count) return;
    float lift = flash * 255;
    GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(count * 4));
    for (int row = 0; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            const Cell *c = &g->cells[row * g->cols + col];
            if (c->glyph <= ' ') continue;
            int index = c->glyph - FONT_FIRST;
            u16 u = (u16)((index % f->columns) * f->cell_w), v = (u16)((index / f->columns) * f->cell_h);
            s16 x = (s16)(g->x0 + col * g->cell_w + (c->nudge ? g->cell_w / 2 : 0)), y = (s16)(g->y0 + row * g->cell_h);
            u32 color = (u32)channel(c->fg.r * gain + lift) << 24 | (u32)channel(c->fg.g * gain + lift) << 16 |
                        (u32)channel(c->fg.b * gain + lift) << 8 | 0xff;
            GX_Position2s16(x, y);
            GX_Color1u32(color);
            GX_TexCoord2u16(u, v);
            GX_Position2s16((s16)(x + g->cell_w), y);
            GX_Color1u32(color);
            GX_TexCoord2u16((u16)(u + f->cell_w), v);
            GX_Position2s16((s16)(x + g->cell_w), (s16)(y + g->cell_h));
            GX_Color1u32(color);
            GX_TexCoord2u16((u16)(u + f->cell_w), (u16)(v + f->cell_h));
            GX_Position2s16(x, (s16)(y + g->cell_h));
            GX_Color1u32(color);
            GX_TexCoord2u16(u, (u16)(v + f->cell_h));
        }
    GX_End();
}

// Phosphor glow: the GPU box-filters the frame into a half-size copy, which goes back on top in soft
// additive taps. The PC renderer's software glow imitates this.
static void glow(float amount) {
    if (!glow_texels || amount <= 0) return;
    GX_SetTexCopySrc(0, 0, FB_W, FB_H);
    GX_SetTexCopyDst(FB_W / 2, FB_H / 2, GX_TF_RGB565, GX_TRUE);
    GX_CopyTex(glow_texels, GX_FALSE);
    GX_PixModeSync();
    GX_InvalidateTexAll();
    GX_LoadTexObj(&glow_tex, GX_TEXMAP0);
    Mtx m;
    guMtxScale(m, 2.0f / FB_W, 2.0f / FB_H, 1);
    GX_LoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
    guMtxIdentity(m);
    GX_LoadPosMtxImm(m, GX_PNMTX0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
    static const s16 taps[4][2] = {{-3, -2}, {3, 2}, {-2, 3}, {2, -3}};
    u8 level = (u8)(amount * 255 / 4 > 255 ? 255 : amount * 255 / 4);
    u32 color = (u32)level << 24 | (u32)level << 16 | (u32)level << 8 | 0xff;
    GX_Begin(GX_QUADS, GX_VTXFMT0, 16);
    for (int k = 0; k < 4; k++) {
        s16 dx = taps[k][0], dy = taps[k][1];
        GX_Position2s16(dx, dy);
        GX_Color1u32(color);
        GX_TexCoord2u16(0, 0);
        GX_Position2s16((s16)(FB_W + dx), dy);
        GX_Color1u32(color);
        GX_TexCoord2u16(FB_W / 2, 0);
        GX_Position2s16((s16)(FB_W + dx), (s16)(FB_H + dy));
        GX_Color1u32(color);
        GX_TexCoord2u16(FB_W / 2, FB_H / 2);
        GX_Position2s16(dx, (s16)(FB_H + dy));
        GX_Color1u32(color);
        GX_TexCoord2u16(0, FB_H / 2);
    }
    GX_End();
    // Back to drawing glyphs for whatever comes next.
    glyph_state();
}

int video_draw(const Screen *s) {
    u64 t0 = gettime();
    GX_SetCopyClear((GXColor){s->fx.bg.r, s->fx.bg.g, s->fx.bg.b, 0xff}, 0x00ffffff);
    glyph_state();
    if (s->scene) draw_grid(s->scene, s->fx.gain, s->fx.flash, s->fx.shake_x, s->fx.shake_y);
    glow(s->fx.glow * s->fx.gain);
    if (s->ui) draw_grid(s->ui, 1, 0, 0, 0);
    u64 t1 = gettime();
    GX_DrawDone();
    build_time = ticks_to_microsecs(t1 - t0);
    gpu_time = ticks_to_microsecs(gettime() - t1);
    front ^= 1;
    GX_CopyDisp(xfb[front], GX_TRUE);
    GX_Flush();
    VIDEO_SetNextFramebuffer(xfb[front]);
    // Never show the framebuffer before the first finished frame is in it.
    if (!shown) {
        VIDEO_SetBlack(FALSE);
        shown = 1;
    }
    VIDEO_Flush();
    // Hold to the frame rhythm: a late frame shows once, then the rhythm picks up again.
    do VIDEO_WaitVSync();
    while (retraces - last_retrace < (u32)interval);
    int elapsed = (int)(retraces - last_retrace);
    last_retrace = retraces;
    return elapsed;
}

void video_set_interval(int count) {
    interval = count < 1 ? 1 : count;
}

int video_interval(void) {
    return interval;
}

double video_time(void) {
    // 50 Hz PAL is the one mode that doesn't refresh at NTSC's 59.94.
    double hz = (mode->viTVMode >> 2) == VI_PAL ? 50.0 : 60000.0 / 1001.0;
    return retraces / hz;
}

void video_times(unsigned *build_us, unsigned *gpu_us) {
    *build_us = build_time;
    *gpu_us = gpu_time;
}

const void *video_xfb(int *width, int *height) {
    *width = mode->fbWidth;
    *height = mode->xfbHeight;
    return xfb[front];
}

void video_shutdown(void) {
    GX_DrawDone();
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}
