// SPDX-License-Identifier: GPL-3.0-only
#include "input.h"

#include <gccore.h>
#include <string.h>
#include <wiikeyboard/keyboard.h>
#include <wiikeyboard/wsksymdef.h>
#include <wiiuse/wpad.h>

static volatile int power_pressed, reset_pressed;
static int keyboard_ready;
static int sideways[4];
static unsigned last_held;
static int repeat_frames;

static void on_reset(u32 irq, void *context) {
    (void)irq;
    (void)context;
    reset_pressed = 1;
}

static void on_power(void) {
    power_pressed = 1;
}

static void on_remote_power(s32 chan) {
    (void)chan;
    power_pressed = 1;
}

void input_init(void) {
    PAD_Init();
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_ALL, FB_W, FB_H);
    WPAD_SetPowerButtonCallback(on_remote_power);
    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);
    // A NULL callback keeps keyboard events in our own queue instead of a second thread feeding stdin.
    keyboard_ready = KEYBOARD_Init(NULL) == 0;
}

int input_power_off(void) {
    return power_pressed;
}

static unsigned remote(int chan, Input *in) {
    u32 type;
    if (WPAD_Probe(chan, &type) != WPAD_ERR_NONE) return 0;
    WPADData *d = WPAD_Data(chan);
    u32 b = d->btns_h, down = d->btns_d;
    unsigned r = 0;

    if (type == WPAD_EXP_CLASSIC) {
        if (b & WPAD_CLASSIC_BUTTON_UP) r |= INPUT_UP;
        if (b & WPAD_CLASSIC_BUTTON_DOWN) r |= INPUT_DOWN;
        if (b & WPAD_CLASSIC_BUTTON_LEFT) r |= INPUT_LEFT;
        if (b & WPAD_CLASSIC_BUTTON_RIGHT) r |= INPUT_RIGHT;
        if (b & WPAD_CLASSIC_BUTTON_A) r |= INPUT_CONFIRM;
        if (b & WPAD_CLASSIC_BUTTON_B) r |= INPUT_BACK;
        if (b & WPAD_CLASSIC_BUTTON_X) r |= INPUT_ALT1;
        if (b & WPAD_CLASSIC_BUTTON_Y) r |= INPUT_ALT2;
        if (b & WPAD_CLASSIC_BUTTON_PLUS) r |= INPUT_PAUSE;
        if (b & WPAD_CLASSIC_BUTTON_HOME) r |= INPUT_HOME;
        if (b & WPAD_CLASSIC_BUTTON_FULL_R) r |= INPUT_VOLUME_UP;
        if (b & WPAD_CLASSIC_BUTTON_FULL_L) r |= INPUT_VOLUME_DOWN;

        float x = d->exp.classic.ljs.mag * __builtin_sinf(d->exp.classic.ljs.ang * 0.0174532925f);
        float y = d->exp.classic.ljs.mag * __builtin_cosf(d->exp.classic.ljs.ang * 0.0174532925f);
        if (y > 0.5f) r |= INPUT_UP;
        if (y < -0.5f) r |= INPUT_DOWN;
        if (x < -0.5f) r |= INPUT_LEFT;
        if (x > 0.5f) r |= INPUT_RIGHT;
        return r;
    }

    // Pressing A means the remote is pointed at the screen; 1 or 2 means it's held sideways.
    if (down & WPAD_BUTTON_A) sideways[chan] = 0;
    if (down & (WPAD_BUTTON_1 | WPAD_BUTTON_2)) sideways[chan] = 1;
    if (sideways[chan]) {
        if (b & WPAD_BUTTON_RIGHT) r |= INPUT_UP;
        if (b & WPAD_BUTTON_LEFT) r |= INPUT_DOWN;
        if (b & WPAD_BUTTON_UP) r |= INPUT_LEFT;
        if (b & WPAD_BUTTON_DOWN) r |= INPUT_RIGHT;
        if (b & WPAD_BUTTON_2) r |= INPUT_CONFIRM;
        if (b & WPAD_BUTTON_1) r |= INPUT_BACK;
        if (b & WPAD_BUTTON_A) r |= INPUT_ALT1;
        if (b & WPAD_BUTTON_B) r |= INPUT_ALT2;
    } else {
        if (b & WPAD_BUTTON_UP) r |= INPUT_UP;
        if (b & WPAD_BUTTON_DOWN) r |= INPUT_DOWN;
        if (b & WPAD_BUTTON_LEFT) r |= INPUT_LEFT;
        if (b & WPAD_BUTTON_RIGHT) r |= INPUT_RIGHT;
        if (b & WPAD_BUTTON_A) r |= INPUT_CONFIRM;
        if (b & WPAD_BUTTON_B) r |= INPUT_BACK;
        if (b & WPAD_BUTTON_1) r |= INPUT_ALT1;
        if (b & WPAD_BUTTON_2) r |= INPUT_ALT2;
    }

    if (b & WPAD_BUTTON_PLUS) r |= INPUT_PAUSE;
    if (b & WPAD_BUTTON_MINUS) r |= INPUT_VOLUME_DOWN;
    if (b & WPAD_BUTTON_HOME) r |= INPUT_HOME;

    if (type == WPAD_EXP_NUNCHUK) {
        float x = d->exp.nunchuk.js.mag * __builtin_sinf(d->exp.nunchuk.js.ang * 0.0174532925f);
        float y = d->exp.nunchuk.js.mag * __builtin_cosf(d->exp.nunchuk.js.ang * 0.0174532925f);
        if (y > 0.5f) r |= INPUT_UP;
        if (y < -0.5f) r |= INPUT_DOWN;
        if (x < -0.5f) r |= INPUT_LEFT;
        if (x > 0.5f) r |= INPUT_RIGHT;
    }

    if (!sideways[chan] && d->ir.valid && !in->pointer) {
        in->pointer = 1;
        in->pointer_x = d->ir.x;
        in->pointer_y = d->ir.y;
    }

    return r;
}

static unsigned gamecube(int chan) {
    u16 b = PAD_ButtonsHeld(chan);
    unsigned r = 0;
    if (b & PAD_BUTTON_UP) r |= INPUT_UP;
    if (b & PAD_BUTTON_DOWN) r |= INPUT_DOWN;
    if (b & PAD_BUTTON_LEFT) r |= INPUT_LEFT;
    if (b & PAD_BUTTON_RIGHT) r |= INPUT_RIGHT;
    if (b & PAD_BUTTON_A) r |= INPUT_CONFIRM;
    if (b & PAD_BUTTON_B) r |= INPUT_BACK;
    if (b & PAD_BUTTON_X) r |= INPUT_ALT1;
    if (b & PAD_BUTTON_Y) r |= INPUT_ALT2;
    if (b & PAD_BUTTON_START) r |= INPUT_PAUSE;
    if (b & PAD_TRIGGER_Z) r |= INPUT_HOME;
    if (b & PAD_TRIGGER_R) r |= INPUT_VOLUME_UP;
    if (b & PAD_TRIGGER_L) r |= INPUT_VOLUME_DOWN;
    s8 x = PAD_StickX(chan), y = PAD_StickY(chan);
    if (y > 50) r |= INPUT_UP;
    if (y < -50) r |= INPUT_DOWN;
    if (x < -50) r |= INPUT_LEFT;
    if (x > 50) r |= INPUT_RIGHT;
    return r;
}

static void type_char(Input *in, char c) {
    if (in->typed_count < INPUT_TYPED_MAX) in->typed[in->typed_count++] = c;
}

static unsigned keyboard(Input *in) {
    unsigned r = 0;
    keyboard_event e;
    while (keyboard_ready && KEYBOARD_GetEvent(&e) > 0) {
        if (e.type != KEYBOARD_PRESSED) continue;
        u16 sym = e.symbol;
        if (sym == KS_Up) r |= INPUT_UP;
        else if (sym == KS_Down) r |= INPUT_DOWN;
        else if (sym == KS_Left) r |= INPUT_LEFT;
        else if (sym == KS_Right) r |= INPUT_RIGHT;
        else if (sym == KS_Escape) r |= INPUT_BACK;
        else if (sym == KS_Return) type_char(in, '\n');
        else if (sym == KS_BackSpace || sym == KS_Delete) type_char(in, '\b');
        else if (sym >= 32 && sym <= 126) type_char(in, (char)sym);
    }
    return r;
}

void input_poll(Input *in) {
    memset(in, 0, sizeof *in);
    WPAD_ScanPads();
    u32 pads = PAD_ScanPads();
    unsigned held = 0;
    for (int chan = 0; chan < 4; chan++) held |= remote(chan, in);
    for (int chan = 0; chan < 4; chan++)
        if (pads & (1u << chan)) held |= gamecube(chan);
    unsigned typed = keyboard(in);
    in->held = held;
    in->pressed = (held & ~last_held) | typed;
    // Held directions repeat, so long lists and the keyboard are quick to cross.
    unsigned dirs = held & (INPUT_UP | INPUT_DOWN | INPUT_LEFT | INPUT_RIGHT);
    if (dirs && dirs == (last_held & (INPUT_UP | INPUT_DOWN | INPUT_LEFT | INPUT_RIGHT))) {
        if (++repeat_frames > 24 && repeat_frames % 5 == 0) in->pressed |= dirs;
    } else {
        repeat_frames = 0;
    }
    last_held = held;
    if (power_pressed || reset_pressed) in->pressed |= INPUT_QUIT;
}

void input_shutdown(void) {
    WPAD_Shutdown();
}
