// SPDX-License-Identifier: GPL-3.0-only
#include "app.h"

#include "figlet.h"
#include "mathx.h"
#include "noise.h"
#include "title.h"
#include "words.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { STATE_TITLE, STATE_COMPOSING, STATE_PLAYING, STATE_PAUSED, STATE_ENDED } State;

typedef enum { MENU_TITLE, MENU_PAUSE, MENU_END } MenuKind;

static const char *const TITLE_ITEMS[] = {"START", "SETTINGS", "EXIT"};
static const char *const PAUSE_ITEMS[] = {"RESUME", "RESTART", "NEW VOYAGE", "TITLE"};
static const char *const END_ITEMS[] = {"PLAY AGAIN", "NEW VOYAGE", "TITLE"};

static const Rgb MENU_DIM = {128, 132, 184}, MENU_LIT = {255, 240, 222}, ACCENT = {255, 106, 138};
static const Rgb FOOTER = {84, 88, 128}, NOTE = {170, 174, 214};

typedef struct {
    MenuKind kind;
    const char *const *items;
    int count, selected, top;
} Menu;

struct App {
    AppConfig config;
    Viewport viewport;
    Grid scene, ui;
    Title title;
    State state;
    Menu menu;
    Piece *piece, *retired;
    char seed[SEED_MAX + 1], status[64];
    double since, frozen;
    float pointer_x, pointer_y;
};

void app_boot(void) {
    mathx_init();
    noise_init();
}

const Viewport *app_viewport(const App *a) {
    return &a->viewport;
}

static void open_menu(App *a, MenuKind kind) {
    a->menu.kind = kind;
    a->menu.selected = 0;
    switch (kind) {
    case MENU_TITLE:
        a->menu.items = TITLE_ITEMS;
        a->menu.count = 3;
        a->menu.top = a->ui.rows / 2 + 1;
        break;
    case MENU_PAUSE:
        a->menu.items = PAUSE_ITEMS;
        a->menu.count = 4;
        a->menu.top = a->ui.rows / 2;
        break;
    case MENU_END:
        a->menu.items = END_ITEMS;
        a->menu.count = 3;
        a->menu.top = a->ui.rows / 2 + 3;
        break;
    }
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
    snprintf(a->seed, sizeof a->seed, "%s", config->last_seed);
    open_menu(a, MENU_TITLE);
    return a;
}

void app_destroy(App *a) {
    if (!a) return;
    title_free(&a->title);
    grid_free(&a->scene);
    grid_free(&a->ui);
    free(a);
}

void app_set_piece(App *a, Piece *p) {
    a->piece = p;
}

Piece *app_release_piece(App *a) {
    Piece *p = a->retired;
    a->retired = NULL;
    return p;
}

// Blanks a grid behind a run of UI cells so text never sits on top of the picture.
static void clear_behind(App *a, Grid *picture, int col, int row, int w, int pad_rows) {
    Grid *ui = &a->ui;
    int x0 = ui->x0 + (col - 1) * ui->cell_w, x1 = ui->x0 + (col + w + 1) * ui->cell_w;
    int y0 = ui->y0 + (row - pad_rows) * ui->cell_h - 2, y1 = ui->y0 + (row + 1 + pad_rows) * ui->cell_h + 2;
    for (int y = (y0 - picture->y0) / picture->cell_h; y <= (y1 - picture->y0) / picture->cell_h; y++)
        for (int x = (x0 - picture->x0) / picture->cell_w; x <= (x1 - picture->x0) / picture->cell_w; x++)
            if (x >= 0 && y >= 0 && x < picture->cols && y < picture->rows) grid_at(picture, x, y)->glyph = ' ';
}

static void text_center(App *a, Grid *picture, int row, const char *text, Rgb color) {
    int w = (int)strlen(text), col = (a->ui.cols - w) / 2;
    if (picture) clear_behind(a, picture, col, row, w, 0);
    grid_text(&a->ui, col, row, text, color);
}

static int menu_row(const Menu *m, int item) {
    return m->top + item * 2;
}

static int menu_input(App *a, const Input *in) {
    Menu *m = &a->menu;
    int moved = in->pointer && (mabs(in->pointer_x - a->pointer_x) > 2 || mabs(in->pointer_y - a->pointer_y) > 2);
    if (in->pointer) {
        a->pointer_x = in->pointer_x;
        a->pointer_y = in->pointer_y;
        int row = (int)((in->pointer_y - (float)a->ui.y0) / (float)a->ui.cell_h);
        // Only a moving pointer takes the selection, so a resting remote never fights the D-pad.
        for (int i = 0; i < m->count && moved; i++)
            if (row == menu_row(m, i)) m->selected = i;
    }
    if (in->pressed & INPUT_UP) m->selected = (m->selected + m->count - 1) % m->count;
    if (in->pressed & INPUT_DOWN) m->selected = (m->selected + 1) % m->count;
    return in->pressed & INPUT_CONFIRM ? m->selected : -1;
}

static void menu_draw(App *a, Grid *picture, float time) {
    const Menu *m = &a->menu;
    for (int i = 0; i < m->count; i++) {
        int row = menu_row(m, i), w = (int)strlen(m->items[i]), col = (a->ui.cols - w) / 2;
        clear_behind(a, picture, col - 3, row, w + 6, 0);
        if (i == m->selected) {
            float pulse = 0.5f + 0.5f * msin(time * 4.2f);
            Rgb arrow = rgb_mix(ACCENT, MENU_LIT, pulse * 0.35f);
            int nudge = pulse > 0.85f ? 1 : 0;
            grid_put(&a->ui, col - 3 + nudge, row, '>', arrow);
            grid_put(&a->ui, col + w + 2 - nudge, row, '<', arrow);
            grid_text(&a->ui, col, row, m->items[i], MENU_LIT);
        } else {
            grid_text(&a->ui, col, row, m->items[i], MENU_DIM);
        }
    }
}

static void request_voyage(App *a, const AppClock *clock, AppRequest *req, int fresh) {
    if (a->config.fixed_seed[0])
        snprintf(a->seed, sizeof a->seed, "%s", a->config.fixed_seed);
    else if (fresh || !a->seed[0])
        seed_random(clock->entropy, a->seed, sizeof a->seed);
    req->compose = 1;
    snprintf(req->seed, sizeof req->seed, "%s", a->seed);
    req->mood = MOOD_ANY;
    req->length = LENGTH_MEDIUM;
    req->save_last_seed = 1;
    snprintf(a->config.last_seed, sizeof a->config.last_seed, "%s", a->seed);
    a->state = STATE_COMPOSING;
    a->since = clock->wall;
}

static void retire_piece(App *a, AppRequest *req) {
    if (!a->piece) return;
    req->stop_music = 1;
    a->retired = a->piece;
    a->piece = NULL;
}

static void title_frame(App *a, const AppClock *clock, const Input *in, Screen *out, AppRequest *req) {
    out->scene = &a->scene;
    title_draw(&a->title, clock->wall, out);
    if (a->state == STATE_COMPOSING) {
        static const char spin[] = "-\\|/";
        char line[64];
        snprintf(line, sizeof line, "weaving %s %c", a->seed, spin[(int)(clock->wall * 8) & 3]);
        text_center(a, &a->scene, a->ui.rows / 2 + 3, line, NOTE);
        return;
    }
    int chosen = menu_input(a, in);
    menu_draw(a, &a->scene, (float)clock->wall);
    char footer[64];
    if (a->config.last_seed[0])
        snprintf(footer, sizeof footer, "last seed: %s", a->config.last_seed);
    else
        snprintf(footer, sizeof footer, "a new voyage every time");
    text_center(a, &a->scene, a->ui.rows - 2, footer, FOOTER);
    if (chosen == 0) request_voyage(a, clock, req, 1);
    if (chosen == 2 || (in->pressed & INPUT_HOME)) req->quit = 1;
}

static void end_card(App *a, Piece *p, float time) {
    Grid *g = &a->ui;
    const SongInfo *info = &p->comp.info;
    const FigletFont *font = figlet_font(FIGLET_SMALL);
    static char art[FIGLET_MAX_HEIGHT * 160];
    char name[SEED_MAX + 1];
    int n = 0;
    for (; info->title[n]; n++) name[n] = info->title[n] >= 'a' && info->title[n] <= 'z' ? (char)(info->title[n] - 32) : info->title[n];
    name[n] = 0;
    int w = font ? figlet_render(font, name, art, 160) : -1;
    int top = a->menu.top - 9;
    if (w > 0 && w <= g->cols) {
        int col = (g->cols - w) / 2;
        clear_behind(a, &p->grid, col, top + 2, w, 3);
        for (int y = 0; y < font->height; y++)
            for (int x = 0; x < w; x++) {
                char ch = art[y * 160 + x];
                if (ch != ' ') grid_put(g, col + x, top + y, ch, rgb_mix(MENU_LIT, rgb(255, 204, 120), (float)y / (float)font->height));
            }
    } else {
        text_center(a, &p->grid, top + 2, name, MENU_LIT);
    }
    char line[96];
    snprintf(line, sizeof line, "%s  %s %s  %d bpm", mood_names[info->mood], note_names[info->key.tonic],
             scale_names[info->key.scale], info->bpm);
    text_center(a, &p->grid, top + 6, line, NOTE);
    (void)time;
}

static void piece_frame(App *a, const AppClock *clock, const Input *in, Screen *out, AppRequest *req) {
    Piece *p = a->piece;
    double t = a->state == STATE_PLAYING ? clock->music : a->frozen;
    if (t < 0) t = 0;
    piece_draw(p, t, out, 0, 0, clock->scope, clock->scope_frames);
    out->ui = &a->ui;
    switch (a->state) {
    case STATE_PLAYING:
        if (in->pressed & (INPUT_CONFIRM | INPUT_PAUSE)) {
            a->state = STATE_PAUSED;
            a->frozen = t;
            req->pause_music = 1;
            open_menu(a, MENU_PAUSE);
        } else if (in->pressed & (INPUT_BACK | INPUT_HOME)) {
            retire_piece(a, req);
            a->state = STATE_TITLE;
            open_menu(a, MENU_TITLE);
        } else if (t >= piece_length(p)) {
            a->state = STATE_ENDED;
            a->frozen = t;
            open_menu(a, MENU_END);
        }
        break;
    case STATE_PAUSED: {
        text_center(a, &p->grid, a->menu.top - 3, "paused", NOTE);
        int chosen = menu_input(a, in);
        menu_draw(a, &p->grid, (float)clock->wall);
        if ((in->pressed & (INPUT_BACK | INPUT_PAUSE)) || chosen == 0) {
            a->state = STATE_PLAYING;
            req->resume_music = 1;
        } else if (chosen == 1) {
            req->stop_music = 1;
            req->start_music = 1;
            a->state = STATE_PLAYING;
        } else if (chosen == 2) {
            retire_piece(a, req);
            request_voyage(a, clock, req, 1);
        } else if (chosen == 3) {
            retire_piece(a, req);
            a->state = STATE_TITLE;
            open_menu(a, MENU_TITLE);
        }
        break;
    }
    case STATE_ENDED: {
        end_card(a, p, (float)clock->wall);
        int chosen = menu_input(a, in);
        menu_draw(a, &p->grid, (float)clock->wall);
        if (chosen == 0) {
            req->stop_music = 1;
            req->start_music = 1;
            a->state = STATE_PLAYING;
        } else if (chosen == 1) {
            retire_piece(a, req);
            request_voyage(a, clock, req, 1);
        } else if (chosen == 2 || (in->pressed & INPUT_BACK)) {
            retire_piece(a, req);
            a->state = STATE_TITLE;
            open_menu(a, MENU_TITLE);
        }
        break;
    }
    default: break;
    }
}

void app_frame(App *a, const AppClock *clock, const Input *in, Screen *out, AppRequest *req) {
    memset(req, 0, sizeof *req);
    grid_clear(&a->ui);
    out->ui = &a->ui;
    if (in->pressed & INPUT_QUIT) req->quit = 1;
    if (a->state == STATE_COMPOSING && a->piece) {
        a->state = STATE_PLAYING;
        req->start_music = 1;
    }
    if (a->state == STATE_TITLE || a->state == STATE_COMPOSING || !a->piece)
        title_frame(a, clock, in, out, req);
    else
        piece_frame(a, clock, in, out, req);
}
