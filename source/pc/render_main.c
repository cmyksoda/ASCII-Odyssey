// SPDX-License-Identifier: GPL-3.0-only
// odyssey-render: the Wii app's core, run on the PC to look at frames and (later) render whole pieces.
#include "../core/app.h"
#include "../core/compose.h"
#include "../core/piece.h"
#include "../core/player.h"
#include "../core/testsong.h"
#include "audio_tools.h"
#include "lab.h"
#include "softgfx.h"

#include <time.h>

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

static float *render_song(const Song *song, long *frames_out) {
    Player p;
    if (player_init(&p, song, 0) != 0) return NULL;
    long cap = AUDIO_RATE * 60, n = 0;
    float *buf = malloc(sizeof(float) * 2 * cap);
    for (;;) {
        if (n + 4096 > cap) {
            cap *= 2;
            buf = realloc(buf, sizeof(float) * 2 * cap);
        }
        int got = player_render(&p, buf + n * 2, 4096);
        n += got;
        if (got < 4096) break;
    }
    player_free(&p);
    *frames_out = n;
    return buf;
}

static int wav_main(int argc, char **argv) {
    const char *out = NULL, *spectro = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
        else if (!strcmp(argv[i], "--spectrogram") && i + 1 < argc)
            spectro = argv[++i];
    }
    if (!out) usage();
    app_boot();
    clock_t t0 = clock();
    Song *song = testsong_create();
    clock_t t1 = clock();
    long frames;
    float *audio = render_song(song, &frames);
    clock_t t2 = clock();
    if (!audio) return 1;
    AudioStats st;
    audio_measure(audio, frames, &st);
    printf("synth %.2fs  render %.2fs for %.1fs of audio (%.0fx realtime)\n", (double)(t1 - t0) / CLOCKS_PER_SEC,
           (double)(t2 - t1) / CLOCKS_PER_SEC, st.seconds, st.seconds / ((double)(t2 - t1) / CLOCKS_PER_SEC));
    printf("peak %.2f dBFS  rms %.2f dBFS  integrated %.1f LUFS  loudest 400ms %.1f LUFS\n", st.peak_db, st.rms_db, st.lufs,
           st.loudest_short_lufs);
    printf("clipped %ld  nonfinite %ld  big jumps %ld\n", st.clipped, st.nonfinite, st.big_jumps);
    wav_write(out, audio, frames);
    if (spectro) spectrogram_png(spectro, audio, frames, 1600, 400);
    free(audio);
    song_free(song);
    return 0;
}

static const char *NOTE_TEXT[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};

// The song as a tracker would show it, for reading the composer's work.
static void dump_song(FILE *f, const Song *s) {
    for (int o = 0; o < s->order_length; o++) {
        const Pattern *p = &s->patterns[s->order[o]];
        fprintf(f, "== pattern %d\n", o);
        for (int r = 0; r < s->pattern_rows; r++) {
            fprintf(f, "%02d|", r);
            for (int c = 0; c < s->channels; c++) {
                const PatternCell *cell = &p->cells[r][c];
                if (cell->note >= 1 && cell->note <= 120)
                    fprintf(f, "%s%d", NOTE_TEXT[(cell->note - 1) % 12], (cell->note - 1) / 12);
                else if (cell->note == NOTE_OFF)
                    fprintf(f, "===");
                else if (cell->note == NOTE_CUT)
                    fprintf(f, "^^^");
                else
                    fprintf(f, "...");
                if (cell->instrument)
                    fprintf(f, "%02d", cell->instrument);
                else
                    fprintf(f, "..");
                fprintf(f, cell->effect ? "%X%02X|" : "...|", cell->effect, cell->param);
            }
            fprintf(f, "\n");
        }
    }
}

static int song_main(int argc, char **argv) {
    const char *out = NULL, *spectro = NULL, *seed = "lunar ribbon", *dump = NULL, *info_path = NULL;
    Mood mood = MOOD_ANY;
    LengthPref length = LENGTH_MEDIUM;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
        else if (!strcmp(argv[i], "--spectrogram") && i + 1 < argc)
            spectro = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            dump = argv[++i];
        else if (!strcmp(argv[i], "--info") && i + 1 < argc)
            info_path = argv[++i];
        else if (!strcmp(argv[i], "--short"))
            length = LENGTH_SHORT;
        else if (!strcmp(argv[i], "--long"))
            length = LENGTH_LONG;
        else if (!strcmp(argv[i], "--mood") && i + 1 < argc) {
            const char *name = argv[++i];
            for (int m = 0; m < MOOD_COUNT; m++)
                if (!strcmp(mood_names[m], name)) mood = (Mood)m;
        }
    }
    app_boot();
    clock_t t0 = clock();
    Composition comp;
    if (compose(seed_hash(seed), seed, mood, length, &comp) != 0) return 1;
    clock_t t1 = clock();
    const SongInfo *info = &comp.info;
    printf("\"%s\": %s, %s %s, %d bpm, %d bars, %.0f s%s\n", info->title, mood_names[info->mood],
           note_names[info->key.tonic], scale_names[info->key.scale], info->bpm, info->bars, info->length,
           info->lift ? " (lifts for the last drop)" : "");
    for (int s = 0; s < info->section_count; s++)
        printf("  %-6s bars %3d-%3d  energy %.2f\n", section_names[info->sections[s].kind], info->sections[s].start_bar,
               info->sections[s].start_bar + info->sections[s].bars - 1, info->sections[s].energy);
    printf("  chords:");
    for (int i = 0; i < info->chord_count && i < 16; i++) printf(" %s%s", note_names[info->chords[i].chord.root_pc], chord_minor(&info->chords[i].chord) ? "m" : "");
    printf("\n  instruments:");
    for (int i = 0; i < comp.song->instrument_count; i++) printf(" %s;", comp.song->instruments[i].name);
    printf("\n");
    if (info_path) {
        FILE *f = fopen(info_path, "w");
        if (f) {
            fprintf(f, "{\"title\": \"%s\", \"mood\": \"%s\", \"key\": \"%s %s\", \"bpm\": %d, \"length\": %.2f, \"sections\": [",
                    info->title, mood_names[info->mood], note_names[info->key.tonic], scale_names[info->key.scale], info->bpm,
                    info->length);
            for (int s = 0; s < info->section_count; s++)
                fprintf(f, "%s{\"kind\": \"%s\", \"start\": %.3f, \"end\": %.3f}", s ? ", " : "",
                        section_names[info->sections[s].kind], info->sections[s].start, info->sections[s].end);
            fprintf(f, "]}\n");
            fclose(f);
        }
    }
    if (dump) {
        FILE *f = fopen(dump, "w");
        if (f) {
            dump_song(f, comp.song);
            fclose(f);
        }
    }
    if (out) {
        long frames;
        float *audio = render_song(comp.song, &frames);
        clock_t t2 = clock();
        AudioStats st;
        audio_measure(audio, frames, &st);
        printf("compose %.2fs  render %.2fs (%.0fx realtime)\n", (double)(t1 - t0) / CLOCKS_PER_SEC,
               (double)(t2 - t1) / CLOCKS_PER_SEC, st.seconds / ((double)(t2 - t1) / CLOCKS_PER_SEC));
        printf("peak %.2f dBFS  integrated %.1f LUFS  loudest 400ms %.1f LUFS  clipped %ld  nonfinite %ld\n", st.peak_db,
               st.lufs, st.loudest_short_lufs, st.clipped, st.nonfinite);
        wav_write(out, audio, frames);
        if (spectro) spectrogram_png(spectro, audio, frames, 1600, 400);
        free(audio);
    }
    composition_free(&comp);
    return 0;
}

static Mood mood_named(const char *name) {
    for (int m = 0; m < MOOD_COUNT; m++)
        if (!strcmp(mood_names[m], name)) return (Mood)m;
    return MOOD_ANY;
}

// Renders a whole piece to video: frames go to ffmpeg raw over a pipe, the audio from a WAV beside it.
static int piece_video(Piece *p, const char *path, int fps, int scale, int wide) {
    long frames;
    float *audio = render_song(p->comp.song, &frames);
    if (!audio) return -1;
    char wav[1024];
    snprintf(wav, sizeof wav, "%s.wav", path);
    wav_write(wav, audio, frames);
    Image fb, shown = {0};
    image_init(&fb, FB_W, FB_H);
    int w = (wide ? FB_W * 4 / 3 : FB_W) * scale, h = FB_H * scale;
    char cmd[2048];
    snprintf(cmd, sizeof cmd,
             "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - -i '%s' -c:v libx264 -preset medium "
             "-crf 18 -tune animation -pix_fmt yuv420p -c:a aac -b:a 192k -shortest -movflags +faststart '%s'",
             w, h, fps, wav, path);
    FILE *pipe = popen(cmd, "w");
    if (!pipe) return -1;
    long total = (long)((double)frames / AUDIO_RATE * fps);
    for (long i = 0; i < total; i++) {
        double t = (double)i / fps;
        long at = (long)(t * AUDIO_RATE) - 512;
        const float *scope = at >= 0 && at + 1024 < frames ? audio + at * 2 : NULL;
        Screen screen = {0};
        piece_draw(p, t, &screen, 0, 0, scope, 1024);
        soft_render(&screen, &fb);
        soft_present(&fb, wide, scale, &shown);
        fwrite(shown.rgb, 1, (size_t)shown.w * shown.h * 3, pipe);
    }
    int rc = pclose(pipe);
    remove(wav);
    image_free(&fb);
    image_free(&shown);
    free(audio);
    return rc;
}

static int piece_main(int argc, char **argv) {
    const char *seed = "lunar ribbon", *sheet = NULL, *video = NULL;
    Mood mood = MOOD_ANY;
    LengthPref length = LENGTH_MEDIUM;
    int wide = 0, fps = 30, scale = 2, count = 0;
    double times[64];
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = argv[++i];
        else if (!strcmp(argv[i], "--mood") && i + 1 < argc)
            mood = mood_named(argv[++i]);
        else if (!strcmp(argv[i], "--sheet") && i + 1 < argc)
            sheet = argv[++i];
        else if (!strcmp(argv[i], "--video") && i + 1 < argc)
            video = argv[++i];
        else if (!strcmp(argv[i], "--times") && i + 1 < argc)
            count = parse_times(argv[++i], times, 64);
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc)
            fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--wide"))
            wide = 1;
        else if (!strcmp(argv[i], "--short"))
            length = LENGTH_SHORT;
        else if (!strcmp(argv[i], "--long"))
            length = LENGTH_LONG;
    }
    app_boot();
    Viewport vp;
    viewport_init(&vp, wide, 0);
    Piece p;
    if (piece_create(&p, seed, mood, length, &vp) != 0) return 1;
    const SongInfo *info = &p.comp.info;
    printf("\"%s\": %s, %s %s, %d bpm, %.0f s, palette %s, grid %dx%d\n", info->title, mood_names[info->mood],
           note_names[info->key.tonic], scale_names[info->key.scale], info->bpm, info->length, p.palette->name, p.grid.cols,
           p.grid.rows);
    for (int i = 0; i < p.direction.count; i++)
        printf("  shot %2d %6.1f-%6.1f s  %-8s enters by %d\n", i, p.direction.shots[i].start, p.direction.shots[i].end,
               scene_kinds[p.direction.shots[i].kind].name, p.direction.shots[i].entrance);
    if (sheet) {
        if (!count) {
            for (int i = 0; i < p.direction.count && count < 12; i++)
                times[count++] = p.direction.shots[i].start + (p.direction.shots[i].end - p.direction.shots[i].start) * 0.4;
        }
        Image fb, shown = {0}, out = {0};
        image_init(&fb, FB_W, FB_H);
        int cols = count < 3 ? count : 3;
        for (int i = 0; i < count; i++) {
            Screen screen = {0};
            piece_draw(&p, times[i], &screen, 0, 0, NULL, 0);
            soft_render(&screen, &fb);
            soft_present(&fb, wide, 1, &shown);
            if (!out.rgb) image_init(&out, shown.w * cols, shown.h * ((count + cols - 1) / cols));
            paste(&out, &shown, (i % cols) * shown.w, (i / cols) * shown.h);
        }
        png_write(sheet, &out);
        image_free(&fb);
        image_free(&shown);
        image_free(&out);
    }
    if (video && piece_video(&p, video, fps, scale, wide) != 0) fprintf(stderr, "video failed\n");
    piece_free(&p);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "piece")) return piece_main(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "wav")) return wav_main(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "song")) return song_main(argc, argv);
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
        AppClock clock = {.wall = times[i], .music = -1};
        AppRequest req;
        app_frame(app, &clock, &in, &screen, &req);
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
