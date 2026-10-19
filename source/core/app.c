// SPDX-License-Identifier: GPL-3.0-only
#include "app.h"

#include "figlet.h"
#include "keyboard.h"
#include "mathx.h"
#include "noise.h"
#include "title.h"
#include "words.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { STATE_TITLE, STATE_SETTINGS, STATE_KEYS, STATE_COMPOSING, STATE_PLAYING, STATE_PAUSED, STATE_ENDED } State;

typedef enum { MENU_TITLE, MENU_PAUSE, MENU_END } MenuKind;

static const char *const TITLE_ITEMS[] = {"START", "SETTINGS", "EXIT"};
static const char *const PAUSE_ITEMS[] = {"RESUME", "RESTART", "NEW VOYAGE", "TITLE"};
static const char *const END_ITEMS[] = {"PLAY AGAIN", "NEW VOYAGE", "TITLE"};

typedef enum {
    ROW_SEED,
    ROW_MOOD,
    ROW_LENGTH,
    ROW_AFTER,
    ROW_VOLUME,
    ROW_CALM,
    ROW_STILL,
    ROW_GLOW,
    ROW_MARGIN,
    ROW_SAVE,
    ROW_DONE,
    ROW_COUNT
} SettingRow;

static const char *const ROW_LABELS[ROW_COUNT] = {"seed", "mood", "length", "after a voyage", "volume", "reduce flashing",
                                                 "reduce motion", "glow", "screen margin", "save last seed", "done"};
static const float GLOW_SCALE[GLOW_LEVELS] = {0, 1, 1.6f};

static const Rgb MENU_DIM = {128, 132, 184}, MENU_LIT = {255, 240, 222}, ACCENT = {255, 106, 138};
static const Rgb FOOTER = {84, 88, 128}, NOTE = {170, 174, 214};

// Seconds the end card waits before sailing on by itself, when that's what the viewer chose.
#define AUTOPLAY_WAIT 8.0

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
    Keyboard keys;
    Piece *piece, *retired;
    char seed[SEED_MAX + 1];
    double since, frozen, ended;
    float pointer_x, pointer_y;
    int setting, settings_changed, waiting;
};

void app_boot(void) {
    mathx_init();
    noise_init();
}

const Viewport *app_viewport(const App *a) {
    return &a->viewport;
}

const Settings *app_settings(const App *a) {
    return &a->config.settings;
}

// Lays the screens out inside the margin the viewer chose; pieces composed afterwards follow it too.
static int layout(App *a) {
    title_free(&a->title);
    grid_free(&a->scene);
    grid_free(&a->ui);
    viewport_init(&a->viewport, a->config.wide, (float)a->config.settings.margin / 100.0f);
    if (grid_init(&a->scene, &a->viewport, 6, 12) != 0 || grid_init(&a->ui, &a->viewport, 8, 16) != 0) return -1;
    return title_init(&a->title, &a->scene);
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
    if (layout(a) != 0) {
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
    int w = (int)strlen(text), nudge, col = grid_center(&a->ui, w, &nudge);
    if (picture) clear_behind(a, picture, col, row, w + nudge, 0);
    grid_text(&a->ui, col, row, text, color);
    if (nudge) grid_nudge(&a->ui, col, row, w);
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
        int row = menu_row(m, i), w = (int)strlen(m->items[i]), nudge, col = grid_center(&a->ui, w, &nudge);
        clear_behind(a, picture, col - 3, row, w + 6 + nudge, 0);
        if (i == m->selected) {
            float pulse = 0.5f + 0.5f * msin(time * 4.2f);
            Rgb arrow = rgb_mix(ACCENT, MENU_LIT, pulse * 0.35f);
            int step = pulse > 0.85f ? 1 : 0;
            grid_put(&a->ui, col - 3 + step, row, '>', arrow);
            grid_put(&a->ui, col + w + 2 - step, row, '<', arrow);
            grid_text(&a->ui, col, row, m->items[i], MENU_LIT);
        } else {
            grid_text(&a->ui, col, row, m->items[i], MENU_DIM);
        }
        // Odd-length labels and their arrows sit half a cell right, so every item shares one center line.
        if (nudge) grid_nudge(&a->ui, col - 3, row, w + 6);
    }
}

// Start sails wherever the seed setting points; New voyage and autoplay always roll a fresh seed.
static void request_voyage(App *a, const AppClock *clock, AppRequest *req, int by_setting) {
    const Settings *s = &a->config.settings;
    if (a->config.fixed_seed[0])
        snprintf(a->seed, sizeof a->seed, "%s", a->config.fixed_seed);
    else if (by_setting && s->seed_mode == SEED_TYPED && s->typed_seed[0])
        snprintf(a->seed, sizeof a->seed, "%s", s->typed_seed);
    else if (by_setting && s->seed_mode == SEED_LAST && a->config.last_seed[0])
        snprintf(a->seed, sizeof a->seed, "%s", a->config.last_seed);
    else
        seed_random(clock->entropy, a->seed, sizeof a->seed);
    req->compose = 1;
    snprintf(req->seed, sizeof req->seed, "%s", a->seed);
    req->mood = (Mood)s->mood;
    req->length = s->length;
    req->save_last_seed = s->save_last_seed;
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

/* ---------- Settings ---------- */

static void setting_value(const App *a, int row, char *out, int size) {
    static const char *const lengths[3] = {"short", "medium", "long"};
    static const char *const afters[AFTER_MODES] = {"show the card", "sail on", "play it again"};
    static const char *const glows[GLOW_LEVELS] = {"off", "soft", "strong"};
    const Settings *s = &a->config.settings;
    switch (row) {
    case ROW_SEED:
        if (s->seed_mode == SEED_TYPED)
            snprintf(out, (size_t)size, "%s", s->typed_seed);
        else if (s->seed_mode == SEED_LAST)
            snprintf(out, (size_t)size, "last: %s", a->config.last_seed[0] ? a->config.last_seed : "none yet");
        else
            snprintf(out, (size_t)size, "a new one each time");
        break;
    case ROW_MOOD: snprintf(out, (size_t)size, "%s", s->mood < 0 ? "any" : mood_names[s->mood]); break;
    case ROW_LENGTH: snprintf(out, (size_t)size, "%s", lengths[s->length]); break;
    case ROW_AFTER: snprintf(out, (size_t)size, "%s", afters[s->after]); break;
    case ROW_VOLUME:
        for (int i = 0; i < VOLUME_STEPS && i < size - 1; i++) out[i] = i < s->volume ? '#' : '-';
        out[VOLUME_STEPS < size - 1 ? VOLUME_STEPS : size - 1] = 0;
        break;
    case ROW_CALM: snprintf(out, (size_t)size, "%s", s->calm ? "on" : "off"); break;
    case ROW_STILL: snprintf(out, (size_t)size, "%s", s->still ? "on" : "off"); break;
    case ROW_GLOW: snprintf(out, (size_t)size, "%s", glows[s->glow]); break;
    case ROW_MARGIN: snprintf(out, (size_t)size, "%d%%", s->margin); break;
    case ROW_SAVE: snprintf(out, (size_t)size, "%s", s->save_last_seed ? "on" : "off"); break;
    default: out[0] = 0; break;
    }
}

static int wrap(int v, int n) {
    return ((v % n) + n) % n;
}

static void setting_step(App *a, int row, int dir) {
    Settings *s = &a->config.settings;
    switch (row) {
    case ROW_SEED: s->seed_mode = (SeedMode)wrap((int)s->seed_mode + dir, s->typed_seed[0] ? SEED_MODES : SEED_TYPED); break;
    case ROW_MOOD: s->mood = wrap(s->mood + 1 + dir, MOOD_COUNT + 1) - 1; break;
    case ROW_LENGTH: s->length = (LengthPref)wrap((int)s->length + dir, 3); break;
    case ROW_AFTER: s->after = (AfterPiece)wrap((int)s->after + dir, AFTER_MODES); break;
    case ROW_VOLUME: s->volume = s->volume + dir < 0 ? 0 : s->volume + dir > VOLUME_STEPS ? VOLUME_STEPS : s->volume + dir; break;
    case ROW_CALM: s->calm = !s->calm; break;
    case ROW_STILL: s->still = !s->still; break;
    case ROW_GLOW: s->glow = (GlowLevel)wrap((int)s->glow + dir, GLOW_LEVELS); break;
    case ROW_MARGIN: {
        int margin = s->margin + dir < 0 ? 0 : s->margin + dir > MARGIN_MAX ? MARGIN_MAX : s->margin + dir;
        if (margin == s->margin) return;
        s->margin = margin;
        layout(a);
        break;
    }
    case ROW_SAVE: s->save_last_seed = !s->save_last_seed; break;
    default: return;
    }
    a->settings_changed = 1;
}

// Rows sit two apart when the screen has room, and pack closer inside a wide margin.
static int setting_y(const App *a, int row) {
    int spacing = a->ui.rows >= ROW_COUNT * 2 + 8 ? 2 : 1;
    return (a->ui.rows - (ROW_COUNT - 1) * spacing) / 2 + 1 + row * spacing;
}

static void leave_settings(App *a, AppRequest *req) {
    if (a->settings_changed) req->save_settings = 1;
    a->settings_changed = 0;
    a->state = STATE_TITLE;
    open_menu(a, MENU_TITLE);
    a->menu.selected = 1;
}

static void settings_frame(App *a, const AppClock *clock, const Input *in, AppRequest *req) {
    Grid *g = &a->ui;
    int moved = in->pointer && (mabs(in->pointer_x - a->pointer_x) > 2 || mabs(in->pointer_y - a->pointer_y) > 2);
    if (in->pointer) {
        a->pointer_x = in->pointer_x;
        a->pointer_y = in->pointer_y;
        int y = (int)((in->pointer_y - (float)g->y0) / (float)g->cell_h);
        for (int row = 0; row < ROW_COUNT && moved; row++)
            if (y == setting_y(a, row)) a->setting = row;
    }
    if (in->pressed & INPUT_UP) a->setting = wrap(a->setting - 1, ROW_COUNT);
    if (in->pressed & INPUT_DOWN) a->setting = wrap(a->setting + 1, ROW_COUNT);
    if (in->pressed & INPUT_LEFT) setting_step(a, a->setting, -1);
    if (in->pressed & INPUT_RIGHT) setting_step(a, a->setting, 1);
    if (in->pressed & INPUT_CONFIRM) {
        if (a->setting == ROW_SEED) {
            keyboard_open(&a->keys, a->config.settings.typed_seed, clock->wall);
            a->state = STATE_KEYS;
            return;
        }
        if (a->setting == ROW_DONE) {
            leave_settings(a, req);
            return;
        }
        setting_step(a, a->setting, 1);
    }
    if (in->pressed & (INPUT_BACK | INPUT_HOME)) {
        leave_settings(a, req);
        return;
    }
    float pulse = 0.5f + 0.5f * msin((float)clock->wall * 4.2f);
    Rgb arrow = rgb_mix(ACCENT, MENU_LIT, pulse * 0.35f);
    int label_col = g->cols / 2 - 22, value_col = g->cols / 2 + 2;
    // The heading gets a wider patch of quiet sky, so no stray star reads as part of it.
    clear_behind(a, &a->scene, g->cols / 2 - 8, setting_y(a, 0) - 3, 16, 1);
    text_center(a, &a->scene, setting_y(a, 0) - 3, "settings", ACCENT);
    for (int row = 0; row < ROW_COUNT; row++) {
        int y = setting_y(a, row), lit = row == a->setting;
        if (row == ROW_DONE) {
            int nudge, col = grid_center(g, 4, &nudge);
            clear_behind(a, &a->scene, col - 3, y, 10 + nudge, 0);
            grid_text(g, col, y, "done", lit ? MENU_LIT : MENU_DIM);
            if (lit) {
                grid_put(g, col - 3, y, '>', arrow);
                grid_put(g, col + 6, y, '<', arrow);
            }
            if (nudge) grid_nudge(g, col - 3, y, 10);
            continue;
        }
        char value[48];
        setting_value(a, row, value, sizeof value);
        clear_behind(a, &a->scene, label_col, y, value_col + 30 - label_col, 0);
        grid_text(g, label_col, y, ROW_LABELS[row], lit ? MENU_LIT : MENU_DIM);
        grid_text(g, value_col, y, value, lit ? MENU_LIT : NOTE);
        if (lit) {
            grid_put(g, value_col - 2, y, '<', arrow);
            grid_put(g, value_col + (int)strlen(value) + 1, y, '>', arrow);
        }
    }
    text_center(a, &a->scene, g->rows - 2, a->setting == ROW_SEED ? "a  type a seed      left right  change      b  back"
                                                                  : "left right  change      a  choose      b  back",
                FOOTER);
}

// The keyboard floats over a quiet patch of sky; done keeps what was typed as the seed to sail to.
static void keys_frame(App *a, const AppClock *clock, const Input *in, AppRequest *req) {
    req->chime = keyboard_update(&a->keys, in, &a->ui, clock->entropy);
    Settings *s = &a->config.settings;
    if (a->keys.state != KEYS_OPEN) {
        if (a->keys.state == KEYS_DONE) {
            snprintf(s->typed_seed, sizeof s->typed_seed, "%s", a->keys.text);
            s->seed_mode = s->typed_seed[0] ? SEED_TYPED : SEED_RANDOM;
            a->settings_changed = 1;
        }
        a->state = STATE_SETTINGS;
        return;
    }
    int col, row, w, h;
    keyboard_extent(&a->ui, &col, &row, &w, &h);
    for (int y = row; y < row + h; y++) clear_behind(a, &a->scene, col, y, w, 0);
    keyboard_draw(&a->keys, &a->ui, clock->wall);
}

static void title_frame(App *a, const AppClock *clock, const Input *in, Screen *out, AppRequest *req) {
    out->scene = &a->scene;
    title_draw(&a->title, clock->wall, out, a->state == STATE_TITLE || a->state == STATE_COMPOSING);
    out->fx.glow *= GLOW_SCALE[a->config.settings.glow];
    if (a->state == STATE_SETTINGS) {
        settings_frame(a, clock, in, req);
        return;
    }
    if (a->state == STATE_KEYS) {
        keys_frame(a, clock, in, req);
        return;
    }
    if (a->state == STATE_COMPOSING) {
        static const char spin[] = "-\\|/";
        char line[64];
        snprintf(line, sizeof line, "weaving %s %c", a->seed, spin[(int)(clock->wall * 8) & 3]);
        text_center(a, &a->scene, a->ui.rows / 2 + 3, line, NOTE);
        return;
    }
    int chosen = menu_input(a, in);
    menu_draw(a, &a->scene, (float)clock->wall);
    const Settings *s = &a->config.settings;
    char footer[64];
    if (a->config.fixed_seed[0])
        snprintf(footer, sizeof footer, "next voyage: %s", a->config.fixed_seed);
    else if (s->seed_mode == SEED_TYPED)
        snprintf(footer, sizeof footer, "next voyage: %s", s->typed_seed);
    else if (s->seed_mode == SEED_LAST && a->config.last_seed[0])
        snprintf(footer, sizeof footer, "next voyage: %s", a->config.last_seed);
    else if (a->config.last_seed[0])
        snprintf(footer, sizeof footer, "last seed: %s", a->config.last_seed);
    else
        snprintf(footer, sizeof footer, "a new voyage every time");
    text_center(a, &a->scene, a->ui.rows - 2, footer, FOOTER);
    if (chosen == 0) request_voyage(a, clock, req, 1);
    if (chosen == 1) {
        a->state = STATE_SETTINGS;
        a->setting = 0;
    }
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
        int nudge, col = grid_center(g, w, &nudge);
        clear_behind(a, &p->grid, col, top + 2, w + nudge, 3);
        for (int y = 0; y < font->height; y++) {
            for (int x = 0; x < w; x++) {
                char ch = art[y * 160 + x];
                if (ch != ' ') grid_put(g, col + x, top + y, ch, rgb_mix(MENU_LIT, rgb(255, 204, 120), (float)y / (float)font->height));
            }
            if (nudge) grid_nudge(g, col, top + y, w);
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
    const Settings *s = &a->config.settings;
    piece_draw(p, t, out, s->calm, s->still, clock->scope, clock->scope_frames);
    out->fx.glow *= GLOW_SCALE[s->glow];
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
        } else if (t >= piece_length(p) && s->after == AFTER_REPLAY) {
            req->stop_music = 1;
            req->start_music = 1;
        } else if (t >= piece_length(p)) {
            a->state = STATE_ENDED;
            a->frozen = t;
            a->ended = clock->wall;
            a->waiting = s->after == AFTER_NEXT;
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
            request_voyage(a, clock, req, 0);
        } else if (chosen == 3) {
            retire_piece(a, req);
            a->state = STATE_TITLE;
            open_menu(a, MENU_TITLE);
        }
        break;
    }
    case STATE_ENDED: {
        end_card(a, p, (float)clock->wall);
        // Sailing on counts down beside the menu; any button keeps the card instead.
        if (a->waiting && in->pressed) a->waiting = 0;
        if (a->waiting) {
            double left = AUTOPLAY_WAIT - (clock->wall - a->ended);
            if (left <= 0) {
                retire_piece(a, req);
                request_voyage(a, clock, req, 0);
                break;
            }
            char line[48];
            snprintf(line, sizeof line, "sailing on in %d", (int)left + 1);
            text_center(a, &p->grid, menu_row(&a->menu, a->menu.count) + 1, line, FOOTER);
        }
        int chosen = menu_input(a, in);
        menu_draw(a, &p->grid, (float)clock->wall);
        if (chosen == 0) {
            req->stop_music = 1;
            req->start_music = 1;
            a->state = STATE_PLAYING;
        } else if (chosen == 1) {
            retire_piece(a, req);
            request_voyage(a, clock, req, 0);
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
    req->chime = CHIME_NONE;
    req->volume = (float)a->config.settings.volume / VOLUME_STEPS;
    grid_clear(&a->ui);
    out->ui = &a->ui;
    if (in->pressed & INPUT_QUIT) req->quit = 1;
    if (a->state == STATE_COMPOSING && a->piece) {
        a->state = STATE_PLAYING;
        req->start_music = 1;
    }
    if (a->state == STATE_TITLE || a->state == STATE_SETTINGS || a->state == STATE_KEYS || a->state == STATE_COMPOSING ||
        !a->piece)
        title_frame(a, clock, in, out, req);
    else
        piece_frame(a, clock, in, out, req);
}
