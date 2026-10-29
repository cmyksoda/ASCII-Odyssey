// SPDX-License-Identifier: GPL-3.0-or-later
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

typedef enum {
    STATE_NOTICE,
    STATE_TITLE,
    STATE_SETTINGS,
    STATE_KEYS,
    STATE_ABOUT,
    STATE_COMPOSING,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_ENDED
} State;

typedef enum { MENU_NOTICE, MENU_TITLE, MENU_PAUSE, MENU_END } MenuKind;

typedef enum { OVERLAY_NONE, OVERLAY_TRACKER, OVERLAY_INFO } Overlay;

static const char *const NOTICE_ITEMS[] = {"SAIL AS IT IS", "SAIL CALMER"};
static const char *const NOTICE_LINES[] = {"a gentle note before you sail", "",
                                           "some voyages will feature flashing lights in time with the music.",
                                           "you can change this any time in settings."};
static const char *const TITLE_ITEMS[] = {"START", "SETTINGS", "ABOUT", "EXIT"};
static const char *const ABOUT_LINES[] = {"how a voyage is made",
                                          "",
                                          "every voyage grows from a seed, a name",
                                          "like violet lagoon. the seed is all it needs.",
                                          "",
                                          "it picks one of ten genres, then a tempo, a key,",
                                          "its instruments and the shape of the song.",
                                          "every note is written fresh and played by a tiny",
                                          "tracker, with sounds built from scratch.",
                                          "",
                                          "the pictures follow along: each part of the song",
                                          "gets its own scenes, colors, and transitions.",
                                          "",
                                          "the same seed always sails the same voyage,",
                                          "so you can share the ones you love <3"};
static const char *const PAUSE_ITEMS[] = {"RESUME", "RESTART", "NEW VOYAGE", "TITLE"};
static const char *const END_ITEMS[] = {"PLAY AGAIN", "NEW VOYAGE", "TITLE"};

enum {
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
};

static const char *const ROW_LABELS[ROW_COUNT] = {"seed", "genre", "length", "after a voyage", "volume",
                                                 "reduce flashing", "reduce motion", "glow", "screen margin",
                                                 "save last seed", "done"};
static const float GLOW_SCALE[GLOW_LEVELS] = {0, 1, 1.6f};

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
    Keyboard keys;
    Piece *piece, *retired;
    // The next voyage, composed in the background while this one plays, so sailing on is instant.
    Piece *next;
    int next_asked, drop_next;
    char seed[SEED_MAX + 1], next_seed[SEED_MAX + 1];
    double since, frozen, volume_shown;
    float pointer_x, pointer_y;
    int setting, settings_changed;
    Overlay overlay;
};

void app_boot(void) {
    mathx_init();
    noise_init();
    // Loaded up front, so a voyage composing in the background never loads a face the screen is reading.
    for (int id = 0; id < FIGLET_FONTS; id++) figlet_font((FigletId)id);
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
    case MENU_NOTICE:
        a->menu.items = NOTICE_ITEMS;
        a->menu.count = 2;
        a->menu.top = a->ui.rows / 2 + 3;
        break;
    case MENU_TITLE:
        a->menu.items = TITLE_ITEMS;
        a->menu.count = 4;
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
    if (config->first_boot) {
        a->state = STATE_NOTICE;
        open_menu(a, MENU_NOTICE);
        return a;
    }
    a->state = STATE_TITLE;
    snprintf(a->seed, sizeof a->seed, "%s", config->last_seed);
    open_menu(a, MENU_TITLE);
    return a;
}

static void free_piece(Piece *p) {
    if (!p) return;
    piece_free(p);
    free(p);
}

void app_destroy(App *a) {
    if (!a) return;
    free_piece(a->next);
    title_free(&a->title);
    grid_free(&a->scene);
    grid_free(&a->ui);
    free(a);
}

void app_set_piece(App *a, Piece *p) {
    if (!a->next_asked) {
        a->piece = p;
        return;
    }
    a->next_asked = 0;
    if (a->drop_next) {
        a->drop_next = 0;
        free_piece(p);
    } else if (a->state == STATE_COMPOSING) {
        a->piece = p;
    } else {
        a->next = p;
    }
}

Piece *app_piece(const App *a) {
    return a->piece;
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
    snprintf(req->last_seed, sizeof req->last_seed, "%s", a->seed);
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

// Composes the voyage after this one in the background, with a seed of its own, as a new voyage would roll it.
static void compose_next(App *a, const AppClock *clock, AppRequest *req) {
    if (a->next || a->next_asked) return;
    const Settings *s = &a->config.settings;
    if (a->config.fixed_seed[0])
        snprintf(a->next_seed, sizeof a->next_seed, "%s", a->config.fixed_seed);
    else
        seed_random(clock->entropy, a->next_seed, sizeof a->next_seed);
    req->compose = 1;
    snprintf(req->seed, sizeof req->seed, "%s", a->next_seed);
    req->mood = (Mood)s->mood;
    req->length = s->length;
    a->next_asked = 1;
}

static void begin_voyage(App *a, const AppClock *clock, AppRequest *req) {
    a->state = STATE_PLAYING;
    req->start_music = 1;
    compose_next(a, clock, req);
}

// Sets out on the next voyage: at once when it's ready, after the weaving screen while it's still composing.
static void sail_on(App *a, const AppClock *clock, AppRequest *req) {
    retire_piece(a, req);
    if (!a->next && !a->next_asked) {
        request_voyage(a, clock, req, 0);
        return;
    }
    snprintf(a->seed, sizeof a->seed, "%s", a->next_seed);
    snprintf(a->config.last_seed, sizeof a->config.last_seed, "%s", a->seed);
    req->save_last_seed = a->config.settings.save_last_seed;
    snprintf(req->last_seed, sizeof req->last_seed, "%s", a->seed);
    a->since = clock->wall;
    a->piece = a->next;
    a->next = NULL;
    if (a->piece)
        begin_voyage(a, clock, req);
    else
        a->state = STATE_COMPOSING;
}

// Back to the title, where the settings may change, so the voyage composed ahead is let go.
static void return_to_title(App *a, AppRequest *req) {
    retire_piece(a, req);
    free_piece(a->next);
    a->next = NULL;
    if (a->next_asked) a->drop_next = 1;
    a->state = STATE_TITLE;
    open_menu(a, MENU_TITLE);
}

/* ---------- Settings ---------- */

static void setting_value(const App *a, int row, char *out, int size) {
    static const char *const lengths[3] = {"short", "medium", "long"};
    static const char *const afters[AFTER_MODES] = {"show menu", "sail on", "play it again"};
    static const char *const glows[GLOW_LEVELS] = {"off", "soft", "strong"};
    const Settings *s = &a->config.settings;
    switch (row) {
    case ROW_SEED:
        if (s->seed_mode == SEED_TYPED)
            snprintf(out, (size_t)size, "%s", s->typed_seed[0] ? s->typed_seed : "type a seed...");
        else if (s->seed_mode == SEED_LAST)
            snprintf(out, (size_t)size, "last: %s", a->config.last_seed[0] ? a->config.last_seed : "none yet");
        else
            snprintf(out, (size_t)size, "random");
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
    case ROW_SEED: s->seed_mode = (SeedMode)wrap((int)s->seed_mode + dir, SEED_MODES); break;
    case ROW_MOOD: {
        static const int ORDER[MOOD_COUNT + 1] = {MOOD_ANY,      MOOD_8BIT,      MOOD_16BIT,  MOOD_AMBIENT,
                                                  MOOD_DUB,      MOOD_DUBSTEP,   MOOD_KEYGEN, MOOD_LOFI,
                                                  MOOD_MUSICBOX, MOOD_SYNTHWAVE, MOOD_TRANCE};
        int at = 0;
        while (at < MOOD_COUNT && ORDER[at] != s->mood) at++;
        s->mood = ORDER[wrap(at + dir, MOOD_COUNT + 1)];
        break;
    }
    case ROW_LENGTH: s->length = (LengthPref)wrap((int)s->length + dir, 3); break;
    case ROW_AFTER: s->after = (AfterPiece)wrap((int)s->after + dir, AFTER_MODES); break;
    case ROW_VOLUME:
        s->volume = s->volume + dir < 0 ? 0 : s->volume + dir > VOLUME_STEPS ? VOLUME_STEPS : s->volume + dir;
        break;
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

// Rows sit two apart whenever the screen can hold them, which every margin the setting offers does, so a margin
// moves the block a row at most. It centers, nudged up when the footer needs the room below.
static int setting_y(const App *a, int row) {
    int rows = a->ui.rows, spacing = rows >= ROW_COUNT * 2 + 3 ? 2 : 1, span = (ROW_COUNT - 1) * spacing;
    int top = (rows - span) / 2 + 1;
    if (spacing == 2 && top > rows - 3 - span) top = rows - 3 - span;
    return top + row * spacing;
}

static int settings_heading_row(const App *a) {
    int top = setting_y(a, 0);
    return top >= 3 ? top - 3 : top - 2;
}

static int settings_footer_row(const App *a) {
    return setting_y(a, ROW_COUNT - 1) + 2 <= a->ui.rows - 2 ? a->ui.rows - 2 : a->ui.rows - 1;
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
    int label_col = g->cols / 2 - 22, value_col = g->cols / 2 + 2, reach = 23;
    // The backing sits evenly about the middle: as far right as the labels reach left, unless a long seed needs more.
    for (int row = 0; row < ROW_DONE; row++) {
        char value[48];
        setting_value(a, row, value, sizeof value);
        int need = value_col + (int)strlen(value) + 4 - g->cols / 2;
        if (need > reach) reach = need;
    }
    // The heading gets a wider patch of quiet sky, so no stray star reads as part of it.
    clear_behind(a, &a->scene, g->cols / 2 - 8, settings_heading_row(a), 16, 1);
    text_center(a, &a->scene, settings_heading_row(a), "settings", ACCENT);
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
        clear_behind(a, &a->scene, g->cols / 2 - reach + 1, y, 2 * reach - 2, 0);
        grid_text(g, label_col, y, ROW_LABELS[row], lit ? MENU_LIT : MENU_DIM);
        grid_text(g, value_col, y, value, lit ? MENU_LIT : NOTE);
        if (lit) {
            grid_put(g, value_col - 2, y, '<', arrow);
            grid_put(g, value_col + (int)strlen(value) + 1, y, '>', arrow);
        }
    }
    text_center(a, &a->scene, settings_footer_row(a),
                a->setting == ROW_SEED ? "a  type a seed      left right  change      b  back"
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

// About, over a quiet patch of sky; any button goes back to the title.
static void about_frame(App *a, const Input *in) {
    int lines = (int)(sizeof ABOUT_LINES / sizeof ABOUT_LINES[0]), top = (a->ui.rows - lines) / 2 - 1;
    for (int i = 0; i < lines; i++) {
        clear_behind(a, &a->scene, a->ui.cols / 2 - 26, top + i, 52, 0);
        if (ABOUT_LINES[i][0]) text_center(a, &a->scene, top + i, ABOUT_LINES[i], i ? NOTE : ACCENT);
    }
    text_center(a, &a->scene, a->ui.rows - 2, "a  b  back", FOOTER);
    if (in->pressed & (INPUT_CONFIRM | INPUT_BACK | INPUT_HOME)) {
        a->state = STATE_TITLE;
        open_menu(a, MENU_TITLE);
        a->menu.selected = 2;
    }
}

// The first boot's note: either answer is saved, so it never comes back.
static void notice_frame(App *a, const AppClock *clock, const Input *in, AppRequest *req) {
    int lines = (int)(sizeof NOTICE_LINES / sizeof NOTICE_LINES[0]), widest = 0;
    for (int i = 0; i < lines; i++)
        widest = (int)strlen(NOTICE_LINES[i]) > widest ? (int)strlen(NOTICE_LINES[i]) : widest;
    for (int i = 0; i < lines; i++) {
        int row = a->menu.top - lines - 2 + i;
        clear_behind(a, &a->scene, a->ui.cols / 2 - widest / 2 - 2, row, widest + 4, 1);
        if (NOTICE_LINES[i][0]) text_center(a, &a->scene, row, NOTICE_LINES[i], i ? NOTE : ACCENT);
    }
    int chosen = menu_input(a, in);
    menu_draw(a, &a->scene, (float)clock->wall);
    if (chosen < 0) return;
    a->config.settings.calm = a->config.settings.still = chosen == 1;
    req->save_settings = 1;
    a->state = STATE_TITLE;
    open_menu(a, MENU_TITLE);
}

static void title_frame(App *a, const AppClock *clock, const Input *in, Screen *out, AppRequest *req) {
    out->scene = &a->scene;
    title_draw(&a->title, clock->wall, out, a->state == STATE_TITLE || a->state == STATE_COMPOSING);
    out->fx.glow *= GLOW_SCALE[a->config.settings.glow];
    if (a->state == STATE_NOTICE) {
        notice_frame(a, clock, in, req);
        return;
    }
    if (a->state == STATE_SETTINGS) {
        settings_frame(a, clock, in, req);
        return;
    }
    if (a->state == STATE_ABOUT) {
        about_frame(a, in);
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
    else if (s->seed_mode == SEED_TYPED && s->typed_seed[0])
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
    if (chosen == 2) a->state = STATE_ABOUT;
    if (chosen == 3 || (in->pressed & INPUT_HOME)) req->quit = 1;
}

static void end_card(App *a, Piece *p, float time) {
    Grid *g = &a->ui;
    const SongInfo *info = &p->comp.info;
    const FigletFont *font = figlet_font(FIGLET_SMALL);
    static char art[FIGLET_MAX_HEIGHT * 160];
    char name[SEED_MAX + 1];
    int n = 0;
    for (; info->title[n]; n++)
        name[n] = info->title[n] >= 'a' && info->title[n] <= 'z' ? (char)(info->title[n] - 32) : info->title[n];
    name[n] = 0;
    int w = font ? figlet_render(font, name, art, 160) : -1;
    int top = a->menu.top - 9;
    if (w > 0 && w <= g->cols) {
        int nudge, col = grid_center(g, w, &nudge);
        clear_behind(a, &p->grid, col, top + 2, w + nudge, 3);
        for (int y = 0; y < font->height; y++) {
            for (int x = 0; x < w; x++) {
                char ch = art[y * 160 + x];
                if (ch != ' ')
                    grid_put(g, col + x, top + y, ch,
                             rgb_mix(MENU_LIT, rgb(255, 204, 120), (float)y / (float)font->height));
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

/* ---------- Overlays: the tracker's view of the song, and what is playing ---------- */

// How hard a channel is sounding: its last note's velocity, fading fast.
static float channel_level(const Score *s, int channel, float t) {
    int lo = 0, hi = s->count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (s->events[mid].time <= t)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (int k = lo - 1; k >= 0 && s->events[k].time > t - 0.5f; k--)
        if (s->events[k].channel == channel) return s->events[k].velocity * mexp(-(t - s->events[k].time) / 0.12f);
    return 0;
}

// FT2's view of the song: its pattern scrolling past the play line, with a meter over every channel it uses.
static void tracker_overlay(App *a, Piece *p, double t) {
    static const char *const notes[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    Grid *g = &a->ui;
    const Song *s = p->comp.song;
    int total = s->order_length * s->pattern_rows, now = (int)(t / song_row_seconds(s));
    now = now < 0 ? 0 : now >= total ? total - 1 : now;
    int used[SONG_CHANNELS], n = 0, fit = (g->cols - 4) / 7;
    for (int ch = 0; ch < s->channels && n < fit; ch++) {
        int any = 0;
        for (int o = 0; o < s->order_length && !any; o++)
            for (int r = 0; r < s->pattern_rows && !any; r++) any = s->patterns[s->order[o]].cells[r][ch].note != 0;
        if (any) used[n++] = ch;
    }
    int width = 3 + n * 7, left = (g->cols - width) / 2, rows = 11, top = g->rows - rows - 2;
    for (int y = top - 1; y < top + rows; y++) clear_behind(a, &p->grid, left, y, width, 0);
    for (int k = 0; k < n; k++) {
        int lit = (int)(channel_level(&p->score, used[k], (float)t) * 6 + 0.5f);
        for (int i = 0; i < 6; i++)
            grid_put(g, left + 3 + k * 7 + i, top - 1, i < lit ? '=' : '.', i < lit ? ACCENT : FOOTER);
    }
    for (int i = 0; i < rows; i++) {
        int row = now - rows / 2 + i;
        if (row < 0 || row >= total) continue;
        int r = row % s->pattern_rows, playing = row == now;
        const Pattern *pat = &s->patterns[s->order[row / s->pattern_rows]];
        Rgb color = playing ? MENU_LIT : r % 4 == 0 ? NOTE : MENU_DIM;
        char text[16];
        snprintf(text, sizeof text, "%02X", r);
        grid_text(g, left, top + i, text, playing ? ACCENT : FOOTER);
        for (int k = 0; k < n; k++) {
            const PatternCell *cell = &pat->cells[r][used[k]];
            if (cell->note >= 1 && cell->note <= 120)
                snprintf(text, sizeof text, "%s%d %02X", notes[(cell->note - 1) % 12], (cell->note - 1) / 12,
                         cell->instrument);
            else
                snprintf(text, sizeof text, "%s ..",
                         cell->note == NOTE_OFF   ? "==="
                         : cell->note == NOTE_CUT ? "^^^"
                                                  : "...");
            grid_text(g, left + 3 + k * 7, top + i, text, color);
        }
    }
}

static void info_overlay(App *a, Piece *p, double t) {
    static const char *const shapes[4] = {"reacts all the way through", "reacts at the big moments", "reactions swell",
                                          "reacts in pockets"};
    const SongInfo *info = &p->comp.info;
    const Section *sec = score_section(&p->score, (float)t);
    const Shot *shot = NULL;
    for (int i = 0; i < p->direction.count; i++)
        if (t >= p->direction.shots[i].start && t < p->direction.shots[i].end) shot = &p->direction.shots[i];
    const char *scene = shot ? scene_kinds[shot->kind].name : "";
    const char *palette = shot ? p->direction.palettes[shot->palette]->name : p->palette->name;
    const char *reacts = p->direction.by_section            ? shapes[p->react_shape]
                         : shot && scene_kinds[shot->kind].reactive ? "answers the music"
                                                                     : "moves on its own";
    char lines[5][64];
    snprintf(lines[0], sizeof lines[0], "%s", info->title);
    snprintf(lines[1], sizeof lines[1], "%s  %s %s  %d bpm", mood_names[info->mood], note_names[info->key.tonic],
             scale_names[info->key.scale], info->bpm);
    snprintf(lines[2], sizeof lines[2], "%s  %d:%02d of %d:%02d", sec ? section_names[sec->kind] : "", (int)t / 60,
             (int)t % 60, (int)info->length / 60, (int)info->length % 60);
    snprintf(lines[3], sizeof lines[3], "%s on %s", scene, palette);
    snprintf(lines[4], sizeof lines[4], "%s", reacts);
    for (int i = 0; i < 5; i++) {
        clear_behind(a, &p->grid, 2, 1 + i, (int)strlen(lines[i]), 0);
        grid_text(&a->ui, 2, 1 + i, lines[i], i ? NOTE : MENU_LIT);
    }
}

static void piece_frame(App *a, const AppClock *clock, const Input *in, Screen *out, AppRequest *req) {
    Piece *p = a->piece;
    double t = a->state == STATE_PLAYING ? clock->music : a->frozen;
    if (t < 0) t = 0;
    const Settings *s = &a->config.settings;
    piece_draw(p, t, out, s->calm, s->still, clock->scope, clock->scope_frames);
    out->fx.glow *= GLOW_SCALE[s->glow];
    out->ui = &a->ui;

    // The volume buttons work all through a voyage, and the new level shows for a moment. The Wii Remote's D-pad joins
    // in only while the music plays, since in a menu it moves the choice.
    unsigned up = INPUT_VOLUME_UP, down = INPUT_VOLUME_DOWN;
    if (a->state == STATE_PLAYING) up |= INPUT_REMOTE_UP, down |= INPUT_REMOTE_DOWN;
    int louder = (in->pressed & up ? 1 : 0) - (in->pressed & down ? 1 : 0);
    if (louder) {
        setting_step(a, ROW_VOLUME, louder);
        req->volume = (float)s->volume / VOLUME_STEPS;
        req->save_settings = 1;
        a->volume_shown = clock->wall + 1.5;
    }
    if (clock->wall < a->volume_shown) {
        char bar[16], line[32];
        setting_value(a, ROW_VOLUME, bar, sizeof bar);
        snprintf(line, sizeof line, "volume %s", bar);
        text_center(a, &p->grid, a->ui.rows - 2, line, NOTE);
    }

    if (a->state == STATE_PLAYING) {
        if (in->pressed & INPUT_ALT1) a->overlay = a->overlay == OVERLAY_TRACKER ? OVERLAY_NONE : OVERLAY_TRACKER;
        if (in->pressed & INPUT_ALT2) a->overlay = a->overlay == OVERLAY_INFO ? OVERLAY_NONE : OVERLAY_INFO;
        if (a->overlay == OVERLAY_TRACKER) tracker_overlay(a, p, t);
        if (a->overlay == OVERLAY_INFO) info_overlay(a, p, t);
    }

    switch (a->state) {
    case STATE_PLAYING:
        if (in->pressed & (INPUT_CONFIRM | INPUT_PAUSE)) {
            a->state = STATE_PAUSED;
            a->frozen = t;
            req->pause_music = 1;
            open_menu(a, MENU_PAUSE);
        } else if (in->pressed & (INPUT_BACK | INPUT_HOME)) {
            return_to_title(a, req);
        } else if (t >= piece_length(p) && s->after == AFTER_REPLAY) {
            req->stop_music = 1;
            req->start_music = 1;
        } else if (t >= piece_length(p) && s->after == AFTER_NEXT) {
            sail_on(a, clock, req);
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
            sail_on(a, clock, req);
        } else if (chosen == 3) {
            return_to_title(a, req);
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
            sail_on(a, clock, req);
        } else if (chosen == 2 || (in->pressed & INPUT_BACK)) {
            return_to_title(a, req);
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
    if (a->state == STATE_COMPOSING && a->piece) begin_voyage(a, clock, req);
    if (a->state == STATE_NOTICE || a->state == STATE_TITLE || a->state == STATE_SETTINGS || a->state == STATE_KEYS ||
        a->state == STATE_ABOUT ||
        a->state == STATE_COMPOSING || !a->piece)
        title_frame(a, clock, in, out, req);
    else
        piece_frame(a, clock, in, out, req);
}
