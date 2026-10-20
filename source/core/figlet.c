// SPDX-License-Identifier: GPL-3.0-only
#include "figlet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SM_EQUAL = 1,
    SM_LOWLINE = 2,
    SM_HIERARCHY = 4,
    SM_PAIR = 8,
    SM_BIGX = 16,
    SM_HARDBLANK = 32,
    SM_KERN = 64,
    SM_SMUSH = 128,
};

#define FIGLET_DATA(id, file) extern const unsigned char flf_##file[]; extern const unsigned int flf_##file##_size;
FIGLET_FACES(FIGLET_DATA)
#undef FIGLET_DATA

static char *next_line(char **cursor) {
    char *line = *cursor;
    if (!*line) return NULL;
    char *end = strchr(line, '\n');
    if (end) {
        *end = 0;
        *cursor = end + 1;
    } else {
        *cursor = line + strlen(line);
    }
    size_t n = strlen(line);
    if (n && line[n - 1] == '\r') line[n - 1] = 0;
    return line;
}

int figlet_load(FigletFont *f, const unsigned char *flf, unsigned int size) {
    memset(f, 0, sizeof *f);
    f->data = malloc(size + 1);
    if (!f->data) return -1;
    memcpy(f->data, flf, size);
    f->data[size] = 0;
    char *cursor = f->data, *header = next_line(&cursor);
    int old_layout = 0, comments = 0, full_layout = -1;
    if (!header || strncmp(header, "flf2a", 5) != 0) return -1;
    f->hardblank = header[5];
    int fields = sscanf(header + 6, "%d %d %*d %d %d %*d %d", &f->height, &f->baseline, &old_layout, &comments,
                        &full_layout);
    if (fields < 4 || f->height < 1 || f->height > FIGLET_MAX_HEIGHT) return -1;
    if (fields >= 5 && full_layout >= 0)
        f->layout = full_layout & 255;
    else
        f->layout = old_layout == 0 ? SM_KERN : old_layout < 0 ? 0 : (old_layout & 31) | SM_SMUSH;
    for (int i = 0; i < comments; i++)
        if (!next_line(&cursor)) return -1;
    for (int g = 0; g < FIGLET_GLYPHS; g++)
        for (int r = 0; r < f->height; r++) {
            char *line = next_line(&cursor);
            if (!line) return -1;
            size_t n = strlen(line);
            // Every row ends in an end mark, the last row of a glyph in two.
            char mark = n ? line[n - 1] : 0;
            while (n && line[n - 1] == mark) line[--n] = 0;
            f->rows[g][r] = line;
            f->widths[g] = (unsigned char)n;
        }
    return 0;
}

static int in(const char *set, char c) {
    return c && strchr(set, c);
}

static char smush(const FigletFont *f, char l, char r, int prev_w, int cur_w) {
    int mode = f->layout;
    if (l == ' ') return r;
    if (r == ' ') return l;
    if (prev_w < 2 || cur_w < 2) return 0;
    if (!(mode & SM_SMUSH)) return 0;
    if (!(mode & 63)) {
        if (l == f->hardblank) return r;
        if (r == f->hardblank) return l;
        return r;
    }
    if ((mode & SM_HARDBLANK) && l == f->hardblank && r == f->hardblank) return l;
    if (l == f->hardblank || r == f->hardblank) return 0;
    if ((mode & SM_EQUAL) && l == r) return l;
    if (mode & SM_LOWLINE) {
        if (l == '_' && in("|/\\[]{}()<>", r)) return r;
        if (r == '_' && in("|/\\[]{}()<>", l)) return l;
    }
    if (mode & SM_HIERARCHY) {
        static const char *const ranks[] = {"|", "/\\", "[]", "{}", "()", "<>"};
        for (int a = 0; a < 6; a++)
            for (int b = a + 1; b < 6; b++) {
                if (in(ranks[a], l) && in(ranks[b], r)) return r;
                if (in(ranks[a], r) && in(ranks[b], l)) return l;
            }
    }
    if (mode & SM_PAIR) {
        if ((l == '[' && r == ']') || (l == ']' && r == '[')) return '|';
        if ((l == '{' && r == '}') || (l == '}' && r == '{')) return '|';
        if ((l == '(' && r == ')') || (l == ')' && r == '(')) return '|';
    }
    if (mode & SM_BIGX) {
        if (l == '/' && r == '\\') return '|';
        if (l == '\\' && r == '/') return 'Y';
        if (l == '>' && r == '<') return 'X';
    }
    return 0;
}

static int overlap(const FigletFont *f, const char *out, int stride, int len, int g, int prev_w) {
    if (!(f->layout & (SM_SMUSH | SM_KERN))) return 0;
    int cur_w = f->widths[g], best = cur_w;
    for (int r = 0; r < f->height; r++) {
        const char *line = out + r * stride, *glyph = f->rows[g][r];
        int bound = len;
        char ch1 = 0;
        for (;;) {
            ch1 = bound < len ? line[bound] : 0;
            if (bound > 0 && (!ch1 || ch1 == ' '))
                bound--;
            else
                break;
        }
        int lead = 0;
        while (glyph[lead] == ' ') lead++;
        char ch2 = glyph[lead];
        int amount = lead + len - 1 - bound;
        if (!ch1 || ch1 == ' ')
            amount++;
        else if (ch2 && smush(f, ch1, ch2, prev_w, cur_w))
            amount++;
        if (amount < best) best = amount;
    }
    return best;
}

int figlet_render(const FigletFont *f, const char *text, char *out, int max_w) {
    memset(out, ' ', (size_t)max_w * f->height);
    int len = 0, prev_w = 0;
    for (const char *p = text; *p; p++) {
        int code = (unsigned char)*p;
        if (code < 32 || code > 126) continue;
        int g = code - 32, cur_w = f->widths[g];
        if (!cur_w) continue;
        int amount = overlap(f, out, max_w, len, g, prev_w);
        if (len - amount + cur_w > max_w) return -1;
        for (int r = 0; r < f->height; r++) {
            char *line = out + r * max_w;
            const char *glyph = f->rows[g][r];
            for (int k = 0; k < amount; k++)
                if (len - amount + k >= 0)
                    line[len - amount + k] = smush(f, line[len - amount + k], glyph[k], prev_w, cur_w);
            memcpy(line + len, glyph + amount, (size_t)(cur_w - amount));
        }
        len += cur_w - amount;
        prev_w = cur_w;
    }
    for (int i = 0; i < max_w * f->height; i++)
        if (out[i] == f->hardblank || !out[i]) out[i] = ' ';
    return len;
}

int figlet_width(const FigletFont *f, const char *text) {
    char scratch[FIGLET_MAX_HEIGHT * 512];
    return figlet_render(f, text, scratch, 512);
}

const FigletFont *figlet_font(FigletId id) {
    static FigletFont fonts[FIGLET_FONTS];
    static int loaded[FIGLET_FONTS];
#define FIGLET_DATA(id, file) flf_##file,
    static const unsigned char *const data[] = {FIGLET_FACES(FIGLET_DATA)};
#undef FIGLET_DATA
#define FIGLET_SIZE(id, file) flf_##file##_size,
    const unsigned int sizes[] = {FIGLET_FACES(FIGLET_SIZE)};
#undef FIGLET_SIZE
    if (id < 0 || id >= FIGLET_FONTS) id = FIGLET_STANDARD;
    if (!loaded[id]) {
        if (figlet_load(&fonts[id], data[id], sizes[id]) != 0) return NULL;
        loaded[id] = 1;
    }
    return &fonts[id];
}

const char *figlet_name(FigletId id) {
#define FIGLET_NAME(id, file) #file,
    static const char *const names[] = {FIGLET_FACES(FIGLET_NAME)};
#undef FIGLET_NAME
    return id >= 0 && id < FIGLET_FONTS ? names[id] : "none";
}

// A line of text rendered into rows, or -1 when it's wider than max_w.
static int wrap_line(const FigletFont *f, const char *text, int max_w, char *rows) {
    int w = figlet_render(f, text, rows, 512);
    return w > max_w ? -1 : w;
}

static int wrap_place(const FigletFont *f, const char *text, int max_w, int max_h, char *out, int stride,
                      FigletLayout *lay) {
    char rows[FIGLET_MAX_HEIGHT * 512];
    int w = wrap_line(f, text, max_w, rows), tall = f->height, top = lay->lines ? lay->height + 1 : 0;
    if (w <= 0 || lay->lines == FIGLET_LINES) return -1;
    for (int blank = 1; blank && tall > 1; tall -= blank)
        for (int x = 0; x < w; x++) blank &= rows[(tall - 1) * 512 + x] == ' ';
    if (top + tall > max_h) return -1;
    for (int y = 0; y < tall; y++) memcpy(out + (top + y) * stride, rows + y * 512, (size_t)w);
    lay->top[lay->lines] = top, lay->wide[lay->lines] = w, lay->tall[lay->lines] = tall;
    lay->lines++;
    lay->height = top + tall;
    if (w > lay->width) lay->width = w;
    return 0;
}

int figlet_wrap(const FigletFont *f, const char *text, int max_w, int max_h, int split, char *out, int stride,
                FigletLayout *layout) {
    char line[128] = "", trial[128], rows[FIGLET_MAX_HEIGHT * 512];
    memset(layout, 0, sizeof *layout);
    if (max_w > stride) return -1;
    memset(out, ' ', (size_t)stride * (size_t)max_h);
    for (const char *s = text; *s;) {
        int gap = 0, end;
        while (s[gap] == ' ') gap++;
        for (end = gap; s[end] && s[end] != ' '; end++) {}
        if (end == gap) break;
        // A word joins the line, with the spaces before it, while the line still fits; otherwise it starts the next.
        if (line[0]) {
            snprintf(trial, sizeof trial, "%s%.*s", line, end, s);
            if (wrap_line(f, trial, max_w, rows) >= 0) {
                memcpy(line, trial, sizeof line);
                s += end;
                continue;
            }
            if (wrap_place(f, line, max_w, max_h, out, stride, layout) != 0) return -1;
            line[0] = 0;
        }
        int keep = end - gap;
        snprintf(trial, sizeof trial, "%.*s", keep, s + gap);
        while (wrap_line(f, trial, max_w, rows) < 0) {
            if (!split || keep == 1) return -1;
            trial[--keep] = 0;
        }
        if (keep < end - gap) {
            if (wrap_place(f, trial, max_w, max_h, out, stride, layout) != 0) return -1;
        } else {
            memcpy(line, trial, sizeof line);
        }
        s += gap + keep;
    }
    if (line[0] && wrap_place(f, line, max_w, max_h, out, stride, layout) != 0) return -1;
    return layout->lines ? 0 : -1;
}
