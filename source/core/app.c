// SPDX-License-Identifier: GPL-3.0-only
#include "app.h"

#include "mathx.h"
#include "noise.h"
#include "title.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MENU_START, MENU_SETTINGS, MENU_EXIT, MENU_ITEMS };

static const char *const menu_names[MENU_ITEMS] = {"START", "SETTINGS", "EXIT"};
static const Rgb MENU_DIM = {128, 132, 184}, MENU_LIT = {255, 240, 222}, ACCENT = {255, 106, 138};
static const Rgb FOOTER = {84, 88, 128};

struct App {
    AppConfig config;
    Viewport viewport;
    Grid scene, ui;
    Title title;
    int selected;
    float pointer_x, pointer_y;
};

void app_boot(void) {
    mathx_init();
    noise_init();
}

App *app_create(const AppConfig *config) {
    App *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->config = *config;
    viewport_init(&a->viewport, config->wide, config->margin);
    if (grid_init(&a->scene, &a->viewport, 6, 12) != 0 || grid_init(&a->ui, &a->viewport, 8, 16) != 0 ||
        title_init(&a->title, &a->scene) != 0) {
        app_destroy(a);
        return NULL;
    }
    return a;
}

void app_destroy(App *a) {
    if (!a) return;
    title_free(&a->title);
    grid_free(&a->scene);
    grid_free(&a->ui);
    free(a);
}

// Blanks the picture behind a run of UI cells so text never sits on top of stars.
static void clear_behind(App *a, int col, int row, int w) {
    Grid *ui = &a->ui, *sc = &a->scene;
    int x0 = ui->x0 + col * ui->cell_w - ui->cell_w, x1 = ui->x0 + (col + w + 1) * ui->cell_w;
    int y0 = ui->y0 + row * ui->cell_h - 2, y1 = ui->y0 + (row + 1) * ui->cell_h + 2;
    for (int y = (y0 - sc->y0) / sc->cell_h; y <= (y1 - sc->y0) / sc->cell_h; y++)
        for (int x = (x0 - sc->x0) / sc->cell_w; x <= (x1 - sc->x0) / sc->cell_w; x++)
            if (x >= 0 && y >= 0 && x < sc->cols && y < sc->rows) grid_at(sc, x, y)->glyph = ' ';
}

static int menu_row(const App *a, int item) {
    return a->ui.rows / 2 + 1 + item * 2;
}

static void ui_text_center(App *a, int row, const char *text, Rgb color) {
    int w = (int)strlen(text), col = (a->ui.cols - w) / 2;
    clear_behind(a, col, row, w);
    grid_text(&a->ui, col, row, text, color);
}

static void draw_menu(App *a, float time) {
    for (int i = 0; i < MENU_ITEMS; i++) {
        int row = menu_row(a, i), w = (int)strlen(menu_names[i]), col = (a->ui.cols - w) / 2;
        clear_behind(a, col - 3, row, w + 6);
        if (i == a->selected) {
            float pulse = 0.5f + 0.5f * msin(time * 4.2f);
            Rgb arrow = rgb_mix(ACCENT, MENU_LIT, pulse * 0.35f);
            int nudge = pulse > 0.85f ? 1 : 0;
            grid_put(&a->ui, col - 3 + nudge, row, '>', arrow);
            grid_put(&a->ui, col + w + 2 - nudge, row, '<', arrow);
            grid_text(&a->ui, col, row, menu_names[i], MENU_LIT);
        } else {
            grid_text(&a->ui, col, row, menu_names[i], MENU_DIM);
        }
    }
    char footer[64];
    if (a->config.last_seed[0])
        snprintf(footer, sizeof footer, "last seed: %s", a->config.last_seed);
    else
        snprintf(footer, sizeof footer, "a new voyage every time");
    ui_text_center(a, a->ui.rows - 2, footer, FOOTER);
}

static int pointer_item(const App *a, const Input *in) {
    if (!in->pointer) return -1;
    int row = (int)((in->pointer_y - (float)a->ui.y0) / (float)a->ui.cell_h);
    for (int i = 0; i < MENU_ITEMS; i++)
        if (row == menu_row(a, i)) return i;
    return -1;
}

int app_frame(App *a, double time, const Input *in, Screen *out) {
    int hover = pointer_item(a, in);
    int moved = in->pointer && (mabs(in->pointer_x - a->pointer_x) > 2 || mabs(in->pointer_y - a->pointer_y) > 2);
    if (in->pointer) {
        a->pointer_x = in->pointer_x;
        a->pointer_y = in->pointer_y;
    }
    // Only a moving pointer takes the selection, so a resting remote never fights the D-pad.
    if (hover >= 0 && moved) a->selected = hover;
    if (in->pressed & INPUT_UP) a->selected = (a->selected + MENU_ITEMS - 1) % MENU_ITEMS;
    if (in->pressed & INPUT_DOWN) a->selected = (a->selected + 1) % MENU_ITEMS;
    if ((in->pressed & INPUT_CONFIRM) && a->selected == MENU_EXIT) return 1;
    if (in->pressed & INPUT_QUIT) return 1;

    out->scene = &a->scene;
    out->ui = &a->ui;
    grid_clear(&a->ui);
    title_draw(&a->title, time, out);
    draw_menu(a, (float)time);
    return 0;
}
