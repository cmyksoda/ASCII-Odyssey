// SPDX-License-Identifier: GPL-3.0-only
// odyssey-render: the Wii app's core, run on the PC to look at frames and (later) render whole pieces.
#include "../core/app.h"
#include "lab.h"
#include "softgfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fprintf(stderr, "usage: odyssey-render title [--wide] [--scale N] [--times t1,t2,...] [--sheet OUT.png | --out DIR]\n");
    exit(2);
}

static int parse_times(const char *text, double *times, int max) {
    int n = 0;
    while (*text && n < max) {
        char *end;
        times[n++] = strtod(text, &end);
        if (*end != ',') break;
        text = end + 1;
    }
    return n;
}

static void paste(Image *sheet, const Image *im, int x0, int y0) {
    for (int y = 0; y < im->h && y0 + y < sheet->h; y++)
        memcpy(&sheet->rgb[((y0 + y) * sheet->w + x0) * 3], &im->rgb[y * im->w * 3], (size_t)im->w * 3);
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "lab")) {
        app_boot();
        return lab_main(argv[2]);
    }
    if (argc < 2 || strcmp(argv[1], "title") != 0) usage();
    AppConfig config = {0};
    int scale = 2;
    double times[64] = {0, 1.5, 3, 4.5};
    int count = 4;
    const char *sheet_path = NULL, *out_dir = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--wide"))
            config.wide = 1;
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--times") && i + 1 < argc)
            count = parse_times(argv[++i], times, 64);
        else if (!strcmp(argv[i], "--sheet") && i + 1 < argc)
            sheet_path = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out_dir = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            snprintf(config.last_seed, sizeof config.last_seed, "%s", argv[++i]);
        else
            usage();
    }
    if (!sheet_path && !out_dir) usage();
    app_boot();
    App *app = app_create(&config);
    if (!app) return 1;
    Image fb, shown = {0}, sheet = {0};
    image_init(&fb, FB_W, FB_H);
    int cols = count < 2 ? 1 : 2;
    for (int i = 0; i < count; i++) {
        Input in = {0};
        Screen screen = {0};
        app_frame(app, times[i], &in, &screen);
        soft_render(&screen, &fb);
        soft_present(&fb, config.wide, sheet_path ? 1 : scale, &shown);
        if (sheet_path) {
            if (!sheet.rgb) image_init(&sheet, shown.w * cols, shown.h * ((count + cols - 1) / cols));
            paste(&sheet, &shown, (i % cols) * shown.w, (i / cols) * shown.h);
        } else {
            char path[512];
            snprintf(path, sizeof path, "%s/title-%06.2f.png", out_dir, times[i]);
            png_write(path, &shown);
        }
    }
    if (sheet_path) png_write(sheet_path, &sheet);
    app_destroy(app);
    return 0;
}
