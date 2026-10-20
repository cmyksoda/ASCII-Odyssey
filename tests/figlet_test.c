// SPDX-License-Identifier: GPL-3.0-only
// Every title face loads, draws only glyphs the font atlas has, lays out like the figlet command, and wraps a long
// title inside the rows it's given.
#include "core/figlet.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static void every_face_loads_with_printable_glyphs(void) {
    for (int id = 0; id < FIGLET_FONTS; id++) {
        const FigletFont *f = figlet_font((FigletId)id);
        CHECK(f != NULL);
        if (!f) {
            printf("  %s didn't load\n", figlet_name((FigletId)id));
            continue;
        }
        int bad = 0;
        for (int g = 0; g < FIGLET_GLYPHS; g++)
            for (int r = 0; r < f->height; r++)
                for (const char *c = f->rows[g][r]; *c; c++) bad += (*c < 32 || *c > 126) && *c != f->hardblank;
        CHECK(!bad);
        if (bad) printf("  %s has %d glyph bytes outside printable ASCII\n", figlet_name((FigletId)id), bad);
    }
}

// What /usr/bin/figlet prints for ODYSSEY in faces that each lay letters together a different way: smushing by
// rules, full width, kerning only, smushing any pair, and smushing equal pairs.
static const struct {
    FigletId id;
    const char *rows[FIGLET_MAX_HEIGHT];
} GOLDEN[] = {
    {FIGLET_DOOM, {
        " _____________   _______ _____ _______   __",
        "|  _  |  _  \\ \\ / /  ___/  ___|  ___\\ \\ / /",
        "| | | | | | |\\ V /\\ `--.\\ `--.| |__  \\ V / ",
        "| | | | | | | \\ /  `--. \\`--. \\  __|  \\ /  ",
        "\\ \\_/ / |/ /  | | /\\__/ /\\__/ / |___  | |  ",
        " \\___/|___/   \\_/ \\____/\\____/\\____/  \\_/  ",
        "                                           ",
        "                                           ",
    }},
    {FIGLET_REV, {
        "======================================================================",
        "===    ====       ===  ====  ===      ====      ===        ==  ====  =",
        "==  ==  ===  ====  ==   ==   ==  ====  ==  ====  ==  ========   ==   =",
        "=  ====  ==  ====  ===  ==  ===  ====  ==  ====  ==  =========  ==  ==",
        "=  ====  ==  ====  ===  ==  ====  ========  =======  =========  ==  ==",
        "=  ====  ==  ====  ====    =======  ========  =====      ======    ===",
        "=  ====  ==  ====  =====  ==========  ========  ===  ===========  ====",
        "=  ====  ==  ====  =====  =====  ====  ==  ====  ==  ===========  ====",
        "==  ==  ===  ====  =====  =====  ====  ==  ====  ==  ===========  ====",
        "===    ====       ======  ======      ====      ===        =====  ====",
        "======================================================================",
    }},
    {FIGLET_COMPUTER, {
        "8\"\"\"88 8\"\"\"\"8 8    8 8\"\"\"\"8 8\"\"\"\"8 8\"\"\"\" 8    8 ",
        "8    8 8    8 8    8 8      8      8     8    8 ",
        "8    8 8e   8 8eeee8 8eeeee 8eeeee 8eeee 8eeee8 ",
        "8    8 88   8   88       88     88 88      88   ",
        "8    8 88   8   88   e   88 e   88 88      88   ",
        "8eeee8 88eee8   88   8eee88 8eee88 88eee   88   ",
        "                                                ",
    }},
    {FIGLET_CRICKET, {
        " _______ ______  ___ ___ _______ _______ _______ ___ ___ ",
        "|   _   |   _  \\|   Y   |   _   |   _   |   _   |   Y   |",
        "|.  |   |.  |   |   1   |   1___|   1___|.  1___|   1   |",
        "|.  |   |.  |    \\_   _/|____   |____   |.  __)_ \\_   _/ ",
        "|:  1   |:  1    /|:  | |:  1   |:  1   |:  1   | |:  |  ",
        "|::.. . |::.. . / |::.| |::.. . |::.. . |::.. . | |::.|  ",
        "`-------`------'  `---' `-------`-------`-------' `---'  ",
        "                                                         ",
    }},
    {FIGLET_LARRY3D, {
        " _____   ____     __    __  ____    ____    ____    __    __ ",
        "/\\  __`\\/\\  _`\\  /\\ \\  /\\ \\/\\  _`\\ /\\  _`\\ /\\  _`\\ /\\ \\  /\\ \\",
        "\\ \\ \\/\\ \\ \\ \\/\\ \\\\ `\\`\\\\/'/\\ \\,\\L\\_\\ \\,\\L\\_\\ \\ \\L\\_\\ `\\`\\\\/'/",
        " \\ \\ \\ \\ \\ \\ \\ \\ \\`\\ `\\ /'  \\/_\\__ \\\\/_\\__ \\\\ \\  _\\L`\\ `\\ /' ",
        "  \\ \\ \\_\\ \\ \\ \\_\\ \\ `\\ \\ \\    /\\ \\L\\ \\/\\ \\L\\ \\ \\ \\L\\ \\`\\ \\ \\ ",
        "   \\ \\_____\\ \\____/   \\ \\_\\   \\ `\\____\\ `\\____\\ \\____/  \\ \\_\\",
        "    \\/_____/\\/___/     \\/_/    \\/_____/\\/_____/\\/___/    \\/_/",
        "                                                             ",
        "                                                             ",
    }},
};

static void faces_match_figlet(void) {
    static char art[FIGLET_MAX_HEIGHT * 200];
    for (unsigned k = 0; k < sizeof GOLDEN / sizeof GOLDEN[0]; k++) {
        const FigletFont *f = figlet_font(GOLDEN[k].id);
        int w = f ? figlet_render(f, "ODYSSEY", art, 200) : -1;
        CHECK(w == (int)strlen(GOLDEN[k].rows[0]));
        for (int r = 0; f && r < f->height && w > 0; r++) {
            int same = GOLDEN[k].rows[r] && !memcmp(art + r * 200, GOLDEN[k].rows[r], (size_t)w);
            CHECK(same);
            if (!same) printf("  %s row %d: %.*s\n", figlet_name(GOLDEN[k].id), r, w, art + r * 200);
        }
    }
}

// The 4:3 grid, keygen's chunky one, and 16:9, less the title card's margins.
static const struct {
    int wide, tall;
} ROOM[] = {{102, 36}, {76, 26}, {124, 36}};

// Long and odd titles the way piece.c spaces them: random ones at their widest, and typed ones.
static const char *const TITLES[] = {
    "WANDERING   MERIDIAN", "LAVENDER   MOUNTAIN", "A",       "THE   QUIET   OLD   HARBOR", "NIGHT   42",
    "HELLO,   WORLD!",      "MIO   &   JAXI",     "SUPERCALIFRAGILISTICEXPI", "A   B   C   D   E   F   G   H"};

static void wrapped_titles_stay_in_their_rows(void) {
    enum { STRIDE = 200, ROWS = FIGLET_LINES * (FIGLET_MAX_HEIGHT + 1), GUARD = 64 };
    static char out[ROWS * STRIDE + GUARD];
    for (int id = 0; id < FIGLET_FONTS; id++) {
        const FigletFont *f = figlet_font((FigletId)id);
        if (!f) continue;
        for (unsigned room = 0; room < sizeof ROOM / sizeof ROOM[0]; room++)
            for (unsigned t = 0; t < sizeof TITLES / sizeof TITLES[0]; t++)
                for (int split = 0; split < 2; split++) {
                    int wide = ROOM[room].wide, tall = ROOM[room].tall;
                    memset(out + tall * STRIDE, '#', (size_t)(ROWS - tall) * STRIDE + GUARD);
                    FigletLayout lay;
                    int rc = figlet_wrap(f, TITLES[t], wide, tall, split, out, STRIDE, &lay);
                    int untouched = 1;
                    for (int i = tall * STRIDE; i < ROWS * STRIDE + GUARD; i++) untouched &= out[i] == '#';
                    CHECK(untouched);
                    if (rc != 0) continue;
                    int ok = lay.lines >= 1 && lay.lines <= FIGLET_LINES && lay.height <= tall && lay.width <= wide;
                    for (int l = 0; l < lay.lines; l++) {
                        ok &= lay.wide[l] >= 1 && lay.wide[l] <= lay.width && lay.tall[l] >= 1;
                        ok &= lay.top[l] == (l ? lay.top[l - 1] + lay.tall[l - 1] + 1 : 0);
                    }
                    ok &= lay.top[lay.lines - 1] + lay.tall[lay.lines - 1] == lay.height;
                    CHECK(ok);
                    if (!ok) printf("  %s, %s in %dx%d\n", figlet_name((FigletId)id), TITLES[t], wide, tall);
                }
    }
}

// However long a typed title is, the plain faces still find room for it on the narrowest grid.
static void plain_faces_always_fit(void) {
    static char out[FIGLET_LINES * (FIGLET_MAX_HEIGHT + 1) * 200];
    const FigletId plain[] = {FIGLET_STANDARD, FIGLET_SLANT, FIGLET_SMALL};
    for (unsigned t = 0; t < sizeof TITLES / sizeof TITLES[0]; t++) {
        int fit = 0;
        FigletLayout lay;
        for (unsigned k = 0; k < sizeof plain / sizeof plain[0] && !fit; k++)
            fit = figlet_wrap(figlet_font(plain[k]), TITLES[t], 76, 26, 1, out, 200, &lay) == 0;
        CHECK(fit);
        if (!fit) printf("  no plain face fits %s\n", TITLES[t]);
    }
}

int main(void) {
    every_face_loads_with_printable_glyphs();
    faces_match_figlet();
    wrapped_titles_stay_in_their_rows();
    plain_faces_always_fit();
    if (!failures) printf("figlet: all passed\n");
    return failures ? 1 : 0;
}
