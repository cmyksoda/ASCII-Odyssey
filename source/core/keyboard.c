// SPDX-License-Identifier: GPL-3.0-only
#include "keyboard.h"

#include "figlet.h"
#include "mathx.h"
#include "song.h"
#include "words.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const KEYS[4] = {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm-.!"};
static const char *const ACTIONS[] = {"space", "delete", "dice", "cancel", "done"};
enum { ACTION_SPACE, ACTION_DELETE, ACTION_DICE, ACTION_CANCEL, ACTION_DONE, ACTION_COUNT };

#define KEY_W 4
#define KEYS_W (10 * KEY_W + 1)
#define PREVIEW_ROWS 6

static const Rgb EDGE = {70, 74, 112}, LABEL = {150, 154, 204}, LIT = {255, 240, 222}, ACCENT = {255, 106, 138};
static const Rgb FIELD = {196, 200, 236}, HINT = {84, 88, 128}, GOLD = {255, 204, 120};

typedef struct {
    int top, left, field_row, keys_row, actions_row, hint_row;
} Layout;

static Layout layout(const Grid *ui) {
    Layout l;
    int height = PREVIEW_ROWS + 2 + 9 + 4 + 2;
    l.top = (ui->rows - height) / 2;
    if (l.top < 0) l.top = 0;
    l.left = (ui->cols - KEYS_W) / 2;
    l.field_row = l.top + PREVIEW_ROWS + 1;
    l.keys_row = l.field_row + 2;
    l.actions_row = l.keys_row + 10;
    l.hint_row = l.actions_row + 4;
    return l;
}

void keyboard_extent(const Grid *ui, int *col, int *row, int *w, int *h) {
    Layout l = layout(ui);
    *col = l.left;
    *row = l.top;
    *w = KEYS_W;
    *h = l.hint_row - l.top + 1;
}

// A soft FM bell: a brief glassy overtone over a sine that rings away.
static void bell(float *out, int frames, float hz, float at, float level) {
    float pc = 0, pm = 0, env = level, index = 1.4f;
    float k_env = mexp(-1.0f / (0.28f * AUDIO_RATE)), k_index = mexp(-1.0f / (0.06f * AUDIO_RATE));
    for (int i = (int)(at * AUDIO_RATE), n = 0; i < frames; i++, n++) {
        pm += hz * 3.5f / AUDIO_RATE;
        pc += hz / AUDIO_RATE;
        if (pm >= 1) pm -= 1;
        if (pc >= 1) pc -= 1;
        out[i] += msin(pc * TAU_F + index * msin(pm * TAU_F)) * env * (n < 96 ? (float)n / 96 : 1);
        env *= k_env;
        index *= k_index;
    }
}

void keyboard_chime(int which, float *out, int frames) {
    static const int steps[CHIME_KEYS] = {0, 2, 4, 7, 9, 12, 14, 16, 19, 21};
    const float c5 = 523.25f;
    memset(out, 0, sizeof(float) * (size_t)frames);
    if (which >= 0 && which < CHIME_KEYS) {
        bell(out, frames, c5 * mexp2((float)steps[which] / 12.0f), 0, 0.5f);
    } else if (which == CHIME_DELETE) {
        bell(out, frames, c5 * 0.75f, 0, 0.35f);
    } else if (which == CHIME_DICE) {
        for (int i = 0; i < 3; i++) bell(out, frames, c5 * mexp2((float)(16 + i * 5) / 12.0f), 0.05f * (float)i, 0.3f);
    } else if (which == CHIME_DONE) {
        static const int chord[4] = {0, 4, 7, 12};
        for (int i = 0; i < 4; i++) bell(out, frames, c5 * mexp2((float)chord[i] / 12.0f), 0.06f * (float)i, 0.3f);
    }
}

// The action keys share the keys' width, each as wide as its label plus padding.
static void action_span(const Layout *l, int index, int *col, int *w) {
    int widths[ACTION_COUNT], total = 0;
    for (int i = 0; i < ACTION_COUNT; i++) total += widths[i] = (int)strlen(ACTIONS[i]) + 4;
    int gap = (KEYS_W - total) / (ACTION_COUNT - 1), x = l->left;
    for (int i = 0; i < index; i++) x += widths[i] + gap;
    *col = x;
    *w = widths[index];
}

void keyboard_open(Keyboard *k, const char *text, double now) {
    memset(k, 0, sizeof *k);
    seed_normalize(text, k->text, sizeof k->text);
    k->len = (int)strlen(k->text);
    k->row = 1;
    k->opened = now;
    k->state = KEYS_OPEN;
}

static int type_char(Keyboard *k, char c) {
    if (c == ' ' && (k->len == 0 || k->text[k->len - 1] == ' ')) return 0;
    if (k->len >= SEED_MAX) return 0;
    k->text[k->len++] = c;
    k->text[k->len] = 0;
    return 1;
}

static int erase(Keyboard *k) {
    if (!k->len) return 0;
    k->text[--k->len] = 0;
    return 1;
}

static void finish(Keyboard *k) {
    char clean[SEED_MAX + 1];
    seed_normalize(k->text, clean, sizeof clean);
    memcpy(k->text, clean, sizeof clean);
    k->len = (int)strlen(k->text);
    k->state = KEYS_DONE;
}

// Which key sits under the pointer, if any.
static int key_at(const Grid *ui, float px, float py, int *row, int *col) {
    Layout l = layout(ui);
    int c = (int)((px - (float)ui->x0) / (float)ui->cell_w), r = (int)((py - (float)ui->y0) / (float)ui->cell_h);
    for (int kr = 0; kr < 4; kr++)
        if (r >= l.keys_row + kr * 2 && r <= l.keys_row + kr * 2 + 2 && c > l.left && c < l.left + KEYS_W - 1) {
            *row = kr;
            *col = (c - l.left) / KEY_W;
            if (*col > 9) *col = 9;
            return 1;
        }
    if (r >= l.actions_row && r <= l.actions_row + 2)
        for (int i = 0; i < ACTION_COUNT; i++) {
            int x, w;
            action_span(&l, i, &x, &w);
            if (c >= x && c < x + w) {
                *row = 4;
                *col = i;
                return 1;
            }
        }
    return 0;
}

// Crossing between the character rows and the action row keeps the selection roughly above or below.
static int action_under(const Layout *l, int key_col) {
    int center = l->left + key_col * KEY_W + 2, best = 0, best_d = 1 << 20;
    for (int i = 0; i < ACTION_COUNT; i++) {
        int x, w;
        action_span(l, i, &x, &w);
        int d = abs(x + w / 2 - center);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static int key_over(const Layout *l, int action) {
    int x, w;
    action_span(l, action, &x, &w);
    int col = (x + w / 2 - l->left) / KEY_W;
    return col < 0 ? 0 : col > 9 ? 9 : col;
}

static int press(Keyboard *k, uint64_t entropy) {
    if (k->row < 4) return type_char(k, KEYS[k->row][k->col]) ? (k->row * 3 + k->col) % CHIME_KEYS : CHIME_NONE;
    switch (k->col) {
    case ACTION_SPACE: return type_char(k, ' ') ? 0 : CHIME_NONE;
    case ACTION_DELETE: return erase(k) ? CHIME_DELETE : CHIME_NONE;
    case ACTION_DICE:
        seed_random(entropy, k->text, sizeof k->text);
        k->len = (int)strlen(k->text);
        return CHIME_DICE;
    case ACTION_CANCEL: k->state = KEYS_CANCELLED; return CHIME_NONE;
    default: finish(k); return CHIME_DONE;
    }
}

int keyboard_update(Keyboard *k, const Input *in, const Grid *ui, uint64_t entropy) {
    Layout l = layout(ui);
    int chime = CHIME_NONE;
    if (in->pointer) {
        int moved = mabs(in->pointer_x - k->pointer_x) > 2 || mabs(in->pointer_y - k->pointer_y) > 2;
        k->pointer_x = in->pointer_x;
        k->pointer_y = in->pointer_y;
        int r, c;
        if (moved && key_at(ui, in->pointer_x, in->pointer_y, &r, &c)) {
            k->row = r;
            k->col = c;
        }
    }
    if (in->pressed & INPUT_UP) {
        if (k->row == 4) k->col = key_over(&l, k->col);
        k->row = k->row == 0 ? 4 : k->row - 1;
        if (k->row == 4) k->col = action_under(&l, k->col);
    }
    if (in->pressed & INPUT_DOWN) {
        if (k->row == 4) k->col = key_over(&l, k->col);
        k->row = k->row == 4 ? 0 : k->row + 1;
        if (k->row == 4) k->col = action_under(&l, k->col);
    }
    int width = k->row == 4 ? ACTION_COUNT : 10;
    if (in->pressed & INPUT_LEFT) k->col = (k->col + width - 1) % width;
    if (in->pressed & INPUT_RIGHT) k->col = (k->col + 1) % width;
    if (in->pressed & INPUT_CONFIRM) chime = press(k, entropy);
    if (in->pressed & INPUT_BACK) {
        if (k->len)
            chime = erase(k) ? CHIME_DELETE : CHIME_NONE;
        else
            k->state = KEYS_CANCELLED;
    }
    if (in->pressed & INPUT_PAUSE) {
        finish(k);
        chime = CHIME_DONE;
    }
    // A USB keyboard types straight in.
    for (int i = 0; i < in->typed_count && k->state == KEYS_OPEN; i++) {
        char c = in->typed[i];
        if (c == '\b') {
            if (erase(k)) chime = CHIME_DELETE;
        } else if (c == '\n') {
            finish(k);
            chime = CHIME_DONE;
        } else if (c >= 32 && c < 127 && type_char(k, c >= 'A' && c <= 'Z' ? (char)(c + 32) : c)) {
            chime = (unsigned char)c % CHIME_KEYS;
        }
    }
    return chime;
}

static void box(Grid *g, int col, int row, int w, Rgb color) {
    for (int x = 1; x < w - 1; x++) {
        grid_put(g, col + x, row, '-', color);
        grid_put(g, col + x, row + 2, '-', color);
    }
    grid_put(g, col, row, '.', color);
    grid_put(g, col + w - 1, row, '.', color);
    grid_put(g, col, row + 2, '\'', color);
    grid_put(g, col + w - 1, row + 2, '\'', color);
    grid_put(g, col, row + 1, '|', color);
    grid_put(g, col + w - 1, row + 1, '|', color);
}

static void preview(const Keyboard *k, Grid *ui, const Layout *l) {
    char upper[SEED_MAX + 1];
    for (int i = 0; i <= k->len; i++) upper[i] = k->text[i] >= 'a' && k->text[i] <= 'z' ? (char)(k->text[i] - 32) : k->text[i];
    const FigletFont *font = figlet_font(FIGLET_SMALL);
    static char art[FIGLET_MAX_HEIGHT * 160];
    int w = font && k->len ? figlet_render(font, upper, art, 160) : -1;
    if (w <= 0 || w > ui->cols - 2) return;
    int col = (ui->cols - w) / 2, rows = font->height < PREVIEW_ROWS ? font->height : PREVIEW_ROWS;
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < w; x++) {
            char ch = art[y * 160 + x];
            if (ch != ' ') grid_put(ui, col + x, l->top + y, ch, rgb_mix(LIT, GOLD, (float)y / (float)(rows - 1)));
        }
}

void keyboard_draw(const Keyboard *k, Grid *ui, double now) {
    Layout l = layout(ui);
    float t = (float)(now - k->opened), pulse = 0.5f + 0.5f * msin(t * 4.2f);
    Rgb accent = rgb_mix(ACCENT, LIT, pulse * 0.35f);
    preview(k, ui, &l);
    // The field: what's typed so far, a blinking cursor, and how much room is left.
    char line[48];
    int field = KEYS_W - 8;
    grid_text(ui, l.left, l.field_row, "seed", HINT);
    grid_text(ui, l.left + 6, l.field_row, k->text, FIELD);
    if (k->len < SEED_MAX && mfract(t * 1.6f) < 0.6f) grid_put(ui, l.left + 6 + k->len, l.field_row, '_', accent);
    snprintf(line, sizeof line, "%2d/%d", k->len, SEED_MAX);
    grid_text(ui, l.left + KEYS_W - (int)strlen(line), l.field_row, line, HINT);
    for (int x = 0; x < field; x++) grid_put(ui, l.left + 6 + x, l.field_row + 1, '-', EDGE);
    // Keycaps share their edges, like a real keyboard's grid.
    for (int r = 0; r < 4; r++) {
        int y = l.keys_row + r * 2;
        for (int c = 0; c < 10; c++) {
            int x = l.left + c * KEY_W;
            grid_put(ui, x, y, r ? '+' : '.', EDGE);
            for (int i = 1; i < KEY_W; i++) grid_put(ui, x + i, y, '-', EDGE);
            grid_put(ui, x, y + 1, '|', EDGE);
            grid_put(ui, x + 2, y + 1, KEYS[r][c], LABEL);
        }
        grid_put(ui, l.left + KEYS_W - 1, y, r ? '+' : '.', EDGE);
        grid_put(ui, l.left + KEYS_W - 1, y + 1, '|', EDGE);
    }
    for (int x = 0; x < KEYS_W; x++) grid_put(ui, l.left + x, l.keys_row + 8, x % KEY_W ? '-' : '\'', EDGE);
    for (int i = 0; i < ACTION_COUNT; i++) {
        int x, w;
        action_span(&l, i, &x, &w);
        box(ui, x, l.actions_row, w, EDGE);
        grid_text(ui, x + 2, l.actions_row + 1, ACTIONS[i], i == ACTION_DONE ? GOLD : LABEL);
    }
    // The selected key lights its own keycap.
    if (k->row < 4) {
        int x = l.left + k->col * KEY_W, y = l.keys_row + k->row * 2;
        box(ui, x, y, KEY_W + 1, accent);
        grid_put(ui, x + 2, y + 1, KEYS[k->row][k->col], LIT);
    } else {
        int x, w;
        action_span(&l, k->col, &x, &w);
        box(ui, x, l.actions_row, w, accent);
        grid_text(ui, x + 2, l.actions_row + 1, ACTIONS[k->col], LIT);
    }
    static const char hint[] = "a  type      b  delete      +  done";
    grid_text(ui, (ui->cols - (int)strlen(hint)) / 2, l.hint_row, hint, HINT);
}
