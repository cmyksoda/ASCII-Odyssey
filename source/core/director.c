// SPDX-License-Identifier: GPL-3.0-only
#include "director.h"

#include "mathx.h"

#include <stdlib.h>
#include <string.h>

// Counted from the list itself, so a missing name stops the build.
static const char *const NAMES[] = {
    "cut",    "dissolve", "wipe",    "diagonal", "radial", "iris",  "blinds", "checker", "spiral", "melt",
    "decode", "fade",     "push",    "doors",    "shatter", "typewriter", "swirl", "zoom", "interlace", "mosaic",
    "slide",  "cascade",  "glitch",  "flip",     "star",   "heart", "kaleido", "tetris", "ripple"};
_Static_assert(sizeof NAMES / sizeof NAMES[0] == ENTRANCES, "every entrance needs a name");
const char *const *const entrance_names = NAMES;

static const Entrance SOFT[] = {ENTER_DISSOLVE, ENTER_WIPE,    ENTER_DIAGONAL,   ENTER_RADIAL,  ENTER_IRIS,      ENTER_BLINDS,
                                ENTER_CHECKER,  ENTER_SPIRAL,  ENTER_MELT,       ENTER_DECODE,  ENTER_FADE,      ENTER_PUSH,
                                ENTER_DOORS,    ENTER_SHATTER, ENTER_TYPEWRITER, ENTER_SWIRL,   ENTER_ZOOM,      ENTER_INTERLACE,
                                ENTER_MOSAIC,   ENTER_SLIDE,   ENTER_CASCADE,    ENTER_GLITCH,  ENTER_FLIP,      ENTER_STAR,
                                ENTER_HEART,    ENTER_KALEIDO, ENTER_TETRIS,     ENTER_RIPPLE};
// A drop lands with a burst that opens from the middle or sweeps across in half a beat.
static const Entrance BURST[] = {ENTER_RADIAL, ENTER_IRIS,   ENTER_CHECKER, ENTER_BLINDS,  ENTER_DIAGONAL,
                                 ENTER_PUSH,   ENTER_DOORS,  ENTER_GLITCH,  ENTER_FLIP,    ENTER_KALEIDO};

static int edm(Mood m) {
    return m == MOOD_SYNTHWAVE || m == MOOD_TRANCE || m == MOOD_KEYGEN || m == MOOD_JUNGLE || m == MOOD_DUBSTEP;
}

// How many shots a section gets: each a few bars long, from about ten seconds in a quiet passage to eight and a half
// in a drop, and never more than twelve.
static int shot_count(const Section *sec, float bar_seconds) {
    float target = 10.5f - 2.0f * sec->energy, length = (float)sec->bars * bar_seconds;
    int n = (int)(length / target + 0.5f), least = (int)(length / 12.0f + 0.999f);
    if (n < least) n = least;
    return n < 1 ? 1 : n > sec->bars ? sec->bars : n;
}

static int single_mood(const SceneKind *s) {
    int moods = 0;
    for (int m = 0; m < MOOD_COUNT; m++) moods += s->weights[m] > 0;
    return moods == 1;
}

// The scene for one shot. Its intensity must suit the music's energy: only scenes within a narrow window of it are
// considered, and the window widens only when nothing fits. Within the window the mood's own scenes come first, then
// those it shows now and then, then stand-ins from other moods. Drops take scenes that answer the music, two of the
// Wii's heaviest scenes never meet in a transition, and a scene comes back only when nothing else is left.
// How hard a piece drives, as a share of its sections' energy, for choosing how intense its scenes may be. Synthwave
// cruises: its drops are its loudest moments, but at its tempo they want scenes a notch calmer than a trance drop's.
static float scene_drive(const SongInfo *info) {
    if (info->mood != MOOD_SYNTHWAVE) return 1.0f;
    return clampf(0.72f + (float)(info->bpm - 95) * 0.0057f, 0.72f, 0.85f);
}

static int choose_scene(Rng *r, Mood mood, const Section *sec, float target, const int *uses, const int *rolled, int previous) {
    static const struct {
        float window;
        int repeats;
    } passes[4] = {{0.25f, 0}, {0.4f, 0}, {0.25f, 1}, {2.0f, 1}};
    for (int pass = 0; pass < 4; pass++) {
        int best = -1;
        float best_cost = 1e9f;
        for (int k = 0; k < scene_kind_count; k++) {
            const SceneKind *s = &scene_kinds[k];
            int weight = s->weights[mood];
            float off = mabs(s->intensity - target);
            if (k == previous || !scene_in_use(s) || off > passes[pass].window || (!weight && single_mood(s))) continue;
            if (uses[k] && !passes[pass].repeats) continue;
            if (previous >= 0 && s->heavy && scene_kinds[previous].heavy) continue;
            float cost = off * 2.0f + rng_float(r) * 0.4f + (float)uses[k] * 3.0f - 0.3f * (float)weight;
            if (weight == 1 && !rolled[k]) cost += 1.0f;
            if (!weight) cost += 2.0f;
            if (sec->kind == SECTION_DROP && !s->reactive) cost += 3.0f;
            if (cost < best_cost) {
                best_cost = cost;
                best = k;
            }
        }
        if (best >= 0) return best;
    }
    return previous >= 0 ? previous : 0;
}

// Each time a piece uses a kind of transition it weighs less there, so the piece wanders through many; the kind just
// used never comes straight back. When every kind has been worn out, any but the last will do.
static Entrance pick_from(Rng *r, const Entrance *list, int count, const int *taste, const int used[ENTRANCES], Entrance previous) {
    int weights[ENTRANCES], total = 0;
    for (int i = 0; i < count; i++) {
        // Scaled up first, so whole-number weights keep the ratios of the penalty.
        weights[i] = list[i] == previous ? 0 : taste[i] * 60 / (1 + 2 * used[list[i]]);
        total += weights[i];
    }
    if (!total)
        for (int i = 0; i < count; i++) weights[i] = list[i] != previous;
    return list[rng_weighted(r, weights, count)];
}

static Entrance pick_burst(Rng *r, const int used[ENTRANCES], Entrance previous) {
    int count = (int)(sizeof BURST / sizeof BURST[0]), taste[sizeof BURST / sizeof BURST[0]];
    for (int i = 0; i < count; i++) taste[i] = 1;
    return pick_from(r, BURST, count, taste, used, previous);
}

// Each mood has its own taste in transitions: keygen's are digital, ambient and the music box melt softly, dubstep
// shoves and shatters, and the music box likes a star or a heart. Weights by family: soft, digital, geometric,
// moving, playful.
enum { FAMILY_SOFT, FAMILY_DIGITAL, FAMILY_GEOMETRIC, FAMILY_MOVING, FAMILY_PLAYFUL, FAMILIES };

static int family_of(Entrance e) {
    switch (e) {
    case ENTER_DISSOLVE:
    case ENTER_FADE:
    case ENTER_MELT:
    case ENTER_SWIRL:
    case ENTER_RIPPLE: return FAMILY_SOFT;
    case ENTER_DECODE:
    case ENTER_TYPEWRITER:
    case ENTER_CHECKER:
    case ENTER_BLINDS:
    case ENTER_INTERLACE:
    case ENTER_MOSAIC:
    case ENTER_GLITCH:
    case ENTER_TETRIS: return FAMILY_DIGITAL;
    case ENTER_PUSH:
    case ENTER_SHATTER:
    case ENTER_ZOOM:
    case ENTER_SLIDE:
    case ENTER_CASCADE:
    case ENTER_FLIP: return FAMILY_MOVING;
    case ENTER_STAR:
    case ENTER_HEART: return FAMILY_PLAYFUL;
    default: return FAMILY_GEOMETRIC;
    }
}

static const int FAMILY_TASTE[MOOD_COUNT][FAMILIES] = {
    [MOOD_KEYGEN] = {1, 5, 2, 2, 1},   [MOOD_AMBIENT] = {5, 1, 2, 1, 1}, [MOOD_SYNTHWAVE] = {1, 2, 3, 3, 1},
    [MOOD_LOFI] = {4, 2, 2, 1, 2},     [MOOD_TRANCE] = {2, 1, 4, 3, 1},  [MOOD_JUNGLE] = {1, 3, 2, 4, 0},
    [MOOD_MUSICBOX] = {5, 1, 2, 1, 3}, [MOOD_DUB] = {4, 1, 2, 2, 1},     [MOOD_DUBSTEP] = {1, 3, 2, 4, 0}};

static Entrance pick_soft_entrance(Rng *r, Mood mood, const int used[ENTRANCES], Entrance previous) {
    int count = (int)(sizeof SOFT / sizeof SOFT[0]), taste[sizeof SOFT / sizeof SOFT[0]];
    for (int i = 0; i < count; i++) taste[i] = FAMILY_TASTE[mood][family_of(SOFT[i])];
    return pick_from(r, SOFT, count, taste, used, previous);
}

// Calm parts paint with the base palette, builds and breaks with their own, and drops take turns between two more.
static int palette_for(SectionKind kind, int drop_shots) {
    switch (kind) {
    case SECTION_DROP: return drop_shots & 1 ? 3 : 1;
    case SECTION_BUILD:
    case SECTION_BREAK: return 2;
    default: return 0;
    }
}

void direction_plan(Direction *d, const Score *score, Rng *r, const Frame *proto,
                    const Palette *const palettes[PALETTE_FAMILY]) {
    const SongInfo *info = score->info;
    Mood mood = info->mood;
    memset(d, 0, sizeof *d);
    for (int i = 0; i < PALETTE_FAMILY; i++) d->palettes[i] = palettes[i];
    // Scenes a mood shows only now and then each join a piece by chance, so two pieces of a mood differ.
    int uses[MAX_SCENE_KINDS] = {0}, rolled[MAX_SCENE_KINDS] = {0}, previous = -1, drop_shots = 0;
    for (int k = 0; k < scene_kind_count; k++) rolled[k] = rng_chance(r, 35);
    Entrance last = ENTER_CUT;
    int used[ENTRANCES] = {0};
    float bar = (float)info->bar_seconds, drive = scene_drive(info);
    for (int s = 0; s < info->section_count && d->count < MAX_SHOTS; s++) {
        const Section *sec = &info->sections[s];
        int pieces = shot_count(sec, bar);
        for (int p = 0; p < pieces && d->count < MAX_SHOTS; p++) {
            Shot *shot = &d->shots[d->count];
            shot->start = (float)sec->start + (float)(p * sec->bars / pieces) * bar;
            shot->end = (float)sec->start + (float)((p + 1) * sec->bars / pieces) * bar;
            shot->target = sec->energy * drive;
            shot->kind = choose_scene(r, mood, sec, shot->target, uses, rolled, previous);
            shot->palette = palette_for(sec->kind, sec->kind == SECTION_DROP ? drop_shots++ : 0);
            shot->direction = rng_range(r, 0, 4);
            if (sec->kind == SECTION_DROP && p == 0 && edm(mood)) {
                shot->entrance = pick_burst(r, used, last);
                shot->fade = clampf(score->beat * 0.5f, 0.18f, 0.35f);
            } else {
                shot->entrance = pick_soft_entrance(r, mood, used, last);
                shot->fade = clampf(score->beat * (sec->energy < 0.5f ? 2.0f : 1.0f), 0.45f, 1.1f);
            }
            last = shot->entrance;
            used[last]++;
            // The change lands with the music: the transition is already more than half done on the bar line.
            if (d->count) {
                shot->start -= shot->fade * 0.6f;
                d->shots[d->count - 1].end = shot->start;
            }
            Frame look = *proto;
            look.palette = palettes[shot->palette];
            Rng shot_rng;
            rng_init(&shot_rng, rng_next(r), "shot");
            scene_kinds[shot->kind].setup(shot->state, &shot_rng, &look);
            uses[shot->kind]++;
            previous = shot->kind;
            d->count++;
        }
    }
    if (d->count) d->shots[d->count - 1].end = score->length + 10;
}

static void render_shot(const Direction *d, Shot *shot, Frame *f, Grid *into) {
    Grid *keep = f->grid;
    Stroker *st = f->stroker;
    Grid *stroke_grid = st->grid;
    f->grid = into;
    st->grid = into;
    f->local = f->t - shot->start;
    f->length = shot->end - shot->start;
    f->palette = d->palettes[shot->palette];
    // Each kind of scene either always answers the music, fully, or never does.
    f->react = d->by_section ? f->section_react : scene_kinds[shot->kind].reactive ? 1.0f : 0.0f;
    grid_clear(into);
    scene_kinds[shot->kind].draw(shot->state, f);
    f->grid = keep;
    st->grid = stroke_grid;
}

static const char SCRAMBLE[] = "!#$%&*+-/:;<=>?@[]^_{|}~";


#define HEART_ANGLES 128

// How far the heart (x^2 + y^2 - 1)^3 = x^2 y^3 reaches from its middle in each direction, found once by bisection.
static float heart_reach(float angle) {
    static float reach[HEART_ANGLES];
    static int ready;
    if (!ready) {
        for (int k = 0; k < HEART_ANGLES; k++) {
            float a = (float)k / HEART_ANGLES * TAU_F, c = mcos(a), s = msin(a), lo = 0, hi = 1.5f;
            for (int it = 0; it < 24; it++) {
                float r = (lo + hi) * 0.5f, x = r * c, y = r * s, q = x * x + y * y - 1;
                if (q * q * q - x * x * y * y * y < 0) lo = r;
                else hi = r;
            }
            reach[k] = lo;
        }
        ready = 1;
    }
    int k = (int)mfloor(angle / TAU_F * HEART_ANGLES + 0.5f);
    return reach[((k % HEART_ANGLES) + HEART_ANGLES) % HEART_ANGLES];
}

// The largest of a point's distance over the heart's reach, taken along the screen's edge where it peaks, so the heart
// covers the farthest corner just as its transition ends. Worked out again only when the screen's shape changes.
static float heart_cover(float aspect) {
    static float for_aspect = -1, cover;
    if (aspect != for_aspect) {
        float reach = 0.5f * msqrt(aspect * aspect + 1);
        cover = 0;
        for (int i = 0; i <= 256; i++) {
            float u = (float)i / 256 - 0.5f, edge[4][2] = {{u * aspect, 0.5f}, {u * aspect, -0.5f}, {0.5f * aspect, u}, {-0.5f * aspect, u}};
            for (int k = 0; k < 4; k++)
                cover = mmax(cover, mhypot(edge[k][0], edge[k][1]) / reach / heart_reach(matan2(edge[k][1], edge[k][0])));
        }
        for_aspect = aspect;
    }
    return cover;
}

// When a cell of the new shot takes over, as a fraction of the transition: each kind of entrance orders the cells its
// own way, with a little per-cell noise so no edge is ruler-straight.
static float reveal_key(Entrance how, int direction, int col, int row, const Grid *g) {
    float x = ((float)col + 0.5f) / (float)g->cols, y = ((float)row + 0.5f) / (float)g->rows;
    float h = hash01((uint32_t)(row * 7919 + col * 104729));
    float dx = (x - 0.5f) * g->aspect, dy = y - 0.5f, reach = 0.5f * msqrt(g->aspect * g->aspect + 1);
    switch (how) {
    case ENTER_WIPE: {
        float along = direction == 0 ? x : direction == 1 ? 1 - x : direction == 2 ? y : 1 - y;
        return along * 0.92f + h * 0.08f;
    }
    case ENTER_DIAGONAL: return ((direction & 1 ? 1 - x : x) + (direction & 2 ? 1 - y : y)) * 0.46f + h * 0.08f;
    case ENTER_RADIAL: return mhypot(dx, dy) / reach * 0.92f + h * 0.08f;
    case ENTER_IRIS: return (1 - mhypot(dx, dy) / reach) * 0.92f + h * 0.08f;
    case ENTER_BLINDS: return ((float)(row % 5) + 0.5f) / 5.0f * 0.9f + h * 0.1f;
    case ENTER_CHECKER: {
        int bx = col / 6, by = row / 3;
        return ((bx + by) & 1 ? 0.5f : 0.0f) + 0.45f * hash01((uint32_t)(bx * 31 + by * 977));
    }
    case ENTER_SPIRAL: {
        float a = (matan2(dy, dx) + PI_F) / TAU_F;
        return mfract(a + mhypot(dx, dy) / reach * 0.75f) * 0.9f + h * 0.1f;
    }
    case ENTER_MELT: return y * 0.55f + hash01((uint32_t)col * 2654435761u) * 0.4f + h * 0.05f;
    // Line by line in reading order, like text typed in.
    case ENTER_TYPEWRITER: return (y + x / (float)g->rows) * 0.97f;
    case ENTER_SWIRL:
    case ENTER_ZOOM: return mhypot(dx, dy) / reach * 0.6f + h * 0.4f;
    case ENTER_STAR: {
        // A five-pointed star, one point up, growing until its shoulders reach the corners.
        float a = matan2(-dy, dx), arm = mabs(mcos(2.5f * (a - PI_F * 0.5f))), shape = 0.55f + 0.45f * arm * arm * arm;
        return mhypot(dx, dy) / reach / shape * 0.55f * 0.94f + h * 0.06f;
    }
    case ENTER_HEART: {
        // A heart growing from the middle until it has covered the farthest corner.
        float fill = mhypot(dx, dy) / reach / heart_reach(matan2(-dy, dx)) / heart_cover(g->aspect);
        return fill * 0.94f + h * 0.06f;
    }
    default: return h;
    }
}

// The new shot as drawn, before the transition writes over its grid, and every cell's reveal key: both grown to fit
// the largest grid seen, so no screen shape ever drops a transition to a plain dissolve.
static Cell *incoming;
static float *key_cache;
static int incoming_cells, key_cache_cells;

// The buffer, grown if need be to hold that many cells; NULL if memory ran out, the old buffer left as it was.
static void *make_room(void *buffer, int *capacity, int cells, size_t size) {
    if (cells <= *capacity) return buffer;
    void *grown = realloc(buffer, size * (size_t)cells);
    if (grown) *capacity = cells;
    return grown;
}

// Every cell's reveal key depends only on the kind of entrance, its direction and the grid, so a transition works them
// out on its first frame and reuses them on the rest. NULL if there was no memory for them.
static const float *reveal_keys(Entrance how, int direction, const Grid *g) {
    static Entrance cached = ENTRANCES;
    static int cached_direction, cached_cols, cached_rows;
    static float cached_aspect;
    float *grown = make_room(key_cache, &key_cache_cells, g->cols * g->rows, sizeof *key_cache);
    if (!grown) return NULL;
    key_cache = grown;
    if (how != cached || direction != cached_direction || g->cols != cached_cols || g->rows != cached_rows || g->aspect != cached_aspect) {
        for (int row = 0; row < g->rows; row++)
            for (int col = 0; col < g->cols; col++) key_cache[row * g->cols + col] = reveal_key(how, direction, col, row, g);
        cached = how, cached_direction = direction, cached_cols = g->cols, cached_rows = g->rows, cached_aspect = g->aspect;
    }
    return key_cache;
}

static const Cell *cell_or_blank(const Cell *cells, const Grid *g, int col, int row) {
    static const Cell blank = {' ', {0, 0, 0}, 0};
    return col < 0 || row < 0 || col >= g->cols || row >= g->rows ? &blank : &cells[row * g->cols + col];
}

// Entrances that move whole pictures instead of revealing cells in place: pushes, doors, interlaced rows, a swirl or
// a zoom on the way out, and the old picture breaking into tiles that fall away. Returns 0 for any other kind.
static int move_pictures(Grid *from, Grid *to, Entrance how, int direction, float p) {
    int cols = to->cols, rows = to->rows, n = cols * rows;
    Cell *grown = make_room(incoming, &incoming_cells, n, sizeof *incoming);
    if (!grown) return 0;
    incoming = grown;
    switch (how) {
    case ENTER_PUSH:
    case ENTER_DOORS:
    case ENTER_INTERLACE:
    case ENTER_SWIRL:
    case ENTER_ZOOM:
    case ENTER_SHATTER:
    case ENTER_MOSAIC:
    case ENTER_SLIDE:
    case ENTER_CASCADE:
    case ENTER_GLITCH:
    case ENTER_FLIP:
    case ENTER_KALEIDO:
    case ENTER_TETRIS:
    case ENTER_RIPPLE: break;
    default: return 0;
    }
    memcpy(incoming, to->cells, sizeof(Cell) * (size_t)n);
    const float *order = reveal_keys(how, direction, to);
    static const Cell blank = {' ', {0, 0, 0}, 0};
    const Cell *before = from->cells;
    const Cell *in = incoming;
    float cx = (float)cols * 0.5f, cy = (float)rows * 0.5f;
    for (int row = 0; row < rows; row++)
        for (int col = 0; col < cols; col++) {
            Cell *dst = grid_at(to, col, row);
            switch (how) {
            case ENTER_PUSH: {
                // The new picture slides in from one side and shoves the old one off the other.
                int horizontal = direction < 2, span = horizontal ? cols : rows, sign = direction & 1 ? -1 : 1;
                int shift = (int)(p * (float)span + 0.5f), at = horizontal ? col : row;
                int old_at = at + sign * shift, new_at = at + sign * (shift - span);
                if (horizontal)
                    *dst = (new_at >= 0 && new_at < span) ? *cell_or_blank(in, to, new_at, row) : *cell_or_blank(from->cells, from, old_at, row);
                else
                    *dst = (new_at >= 0 && new_at < span) ? *cell_or_blank(in, to, col, new_at) : *cell_or_blank(from->cells, from, col, old_at);
                break;
            }
            case ENTER_DOORS: {
                // The old picture parts down the middle, each half sliding away, the new one waiting behind.
                int half = cols / 2, shift = (int)(p * (float)half + 0.5f);
                int src = col < half ? col + shift : col - shift;
                int inside = col < half ? src < half : src >= half;
                *dst = inside ? *cell_or_blank(from->cells, from, src, row) : in[row * cols + col];
                break;
            }
            case ENTER_INTERLACE: {
                // Alternate rows of the new picture race in from opposite sides.
                int shift = (int)((1 - p) * (float)cols + 0.5f), src = row & 1 ? col + shift : col - shift;
                *dst = src >= 0 && src < cols ? in[row * cols + src] : *cell_or_blank(from->cells, from, col, row);
                break;
            }
            case ENTER_SWIRL:
            case ENTER_ZOOM: {
                // The old picture twists like water down a drain, or rushes toward us, as the new one shows through.
                float key = order ? order[row * cols + col] : reveal_key(how, direction, col, row, to);
                if (key <= p) {
                    *dst = in[row * cols + col];
                    break;
                }
                // In row heights, so the swirl turns round rather than oval.
                float ratio = (float)to->cell_w * to->pixel_aspect / (float)to->cell_h, dx = ((float)col - cx) * ratio, dy = (float)row - cy;
                float sx, sy;
                if (how == ENTER_SWIRL) {
                    float r = mhypot(dx, dy) / cy, a = p * 3.0f * (1 - saturate(r * 0.8f)), c = mcos(a), s = msin(a);
                    sx = dx * c - dy * s, sy = dx * s + dy * c;
                } else {
                    float k = 1 / (1 + p * 1.5f);
                    sx = dx * k, sy = dy * k;
                }
                int src_col = (int)(cx + sx / ratio + 0.5f), src_row = (int)(cy + sy + 0.5f);
                *dst = *cell_or_blank(from->cells, from, src_col, src_row);
                break;
            }
            case ENTER_MOSAIC: {
                // The old picture coarsens into blocks, and the new one sharpens back out of them.
                float coarse = 1 - mabs(2 * p - 1);
                // Blocks as tall as they are wide on screen, given the cells' shape.
                int bw = 1 + (int)(coarse * 10), bh = 1 + (int)(coarse * 10 * (float)to->cell_w * to->pixel_aspect / (float)to->cell_h);
                *dst = *cell_or_blank(p < 0.5f ? before : in, to, col / bw * bw + bw / 2, row / bh * bh + bh / 2);
                break;
            }
            case ENTER_SLIDE: {
                // The new picture slides in over the old one, which stays where it is.
                int horizontal = direction < 2, span = horizontal ? cols : rows, sign = direction & 1 ? -1 : 1;
                int at = horizontal ? col : row, new_at = at + sign * ((int)(p * (float)span + 0.5f) - span);
                if (new_at >= 0 && new_at < span)
                    *dst = horizontal ? in[row * cols + new_at] : in[new_at * cols + col];
                else
                    *dst = before[row * cols + col];
                break;
            }
            case ENTER_CASCADE: {
                // Column by column the new picture drops in from above, each after its own short wait, under gravity.
                float age = (p - hash01((uint32_t)col * 2654435761u) * 0.5f) / 0.5f;
                int fall = age >= 1 ? 0 : (int)((1 - age * age) * (float)rows);
                *dst = age > 0 && row + fall < rows ? in[(row + fall) * cols + col] : before[row * cols + col];
                break;
            }
            case ENTER_GLITCH: {
                // Bands of rows tear sideways by ragged amounts that swell and settle, each band jumping to the new
                // picture at its own moment.
                uint32_t band = (uint32_t)(row / 2) * 2654435761u, tear = band ^ (uint32_t)(p * 6.0f) * 40503u;
                int shift = (int)((hash01(tear) - 0.5f) * msin(p * PI_F) * (float)cols * 0.5f);
                *dst = *cell_or_blank(hash01(band + 7) < p ? in : before, to, col - shift, row);
                break;
            }
            case ENTER_FLIP: {
                // Like an old television changing channel: the old picture squashes to a line, the new one springs open.
                float height = mabs(2 * p - 1), rel = ((float)row + 0.5f - cy) / (height * cy + 0.001f);
                if (mabs(rel) > 1)
                    *dst = height < 0.06f && row == rows / 2 ? (Cell){'-', {235, 235, 235}, 0} : blank;
                else
                    *dst = *cell_or_blank(p < 0.5f ? before : in, to, col, (int)(cy + rel * cy));
                break;
            }
            case ENTER_KALEIDO: {
                // The old picture folds into a kaleidoscope of more and more mirrors, then the new one unfolds from it.
                int mirrors = (int)((1 - mabs(2 * p - 1)) * 8.99f);
                const Cell *src = p < 0.5f ? before : in;
                if (mirrors < 2) {
                    *dst = src[row * cols + col];
                    break;
                }
                float ratio = (float)to->cell_w * to->pixel_aspect / (float)to->cell_h, dx = ((float)col - cx) * ratio, dy = (float)row - cy;
                float r = mhypot(dx, dy), wedge = PI_F / (float)mirrors, a = matan2(dy, dx) + PI_F;
                a -= mfloor(a / (2 * wedge)) * 2 * wedge;
                if (a > wedge) a = 2 * wedge - a;
                *dst = *cell_or_blank(src, to, (int)(cx + r * mcos(a) / ratio + 0.5f), (int)(cy + r * msin(a) + 0.5f));
                break;
            }
            case ENTER_TETRIS: *dst = before[row * cols + col]; break;
            case ENTER_RIPPLE: {
                // A ring of water spreads from the middle: the new picture lies inside it, and the pictures either side
                // of the wavefront bend toward and away from it.
                float ratio = (float)to->cell_w * to->pixel_aspect / (float)to->cell_h, dx = ((float)col - cx) * ratio, dy = (float)row - cy;
                float d = mhypot(dx, dy), reach = mhypot(cx * ratio, cy) * 1.15f, front = p * reach, band = 3.0f;
                const Cell *src = d < front ? in : before;
                if (mabs(d - front) < band && d > 0.5f) {
                    float bend = 2.5f * (1 - p) * msin((d - front) / band * PI_F), at = (d + bend) / d;
                    *dst = *cell_or_blank(src, to, (int)(cx + dx * at / ratio + 0.5f), (int)(cy + dy * at + 0.5f));
                } else {
                    *dst = src[row * cols + col];
                }
                break;
            }
            default: *dst = in[row * cols + col]; break;
            }
        }
    if (how == ENTER_TETRIS) {
        // Blocks of the new picture drop in from above and stack up from the bottom row, each landing exactly in place.
        int bw = 4, bh = (int)(4.0f * (float)to->cell_w * to->pixel_aspect / (float)to->cell_h + 0.5f), stacks = (rows + bh - 1) / bh;
        if (bh < 1) bh = 1;
        for (int by = stacks - 1; by >= 0; by--)
            for (int bx = 0; bx * bw < cols; bx++) {
                float order = (float)(stacks - 1 - by) / (float)stacks * 0.7f + hash01((uint32_t)(by * 977 + bx) ^ 0x7e7u) * 0.3f;
                float age = (p - order * 0.75f) / 0.25f;
                if (age <= 0) continue;
                int top = by * bh, fall = age >= 1 ? 0 : (int)((1 - age * age) * (float)(top + bh));
                for (int y = top; y < top + bh && y < rows; y++)
                    for (int x = bx * bw; x < bx * bw + bw && x < cols; x++)
                        if (y - fall >= 0) *grid_at(to, x, y - fall) = in[y * cols + x];
            }
    }
    if (how == ENTER_SHATTER) {
        // The old picture breaks into tiles, each dropping away under gravity after a moment of its own.
        for (int ty = 0; ty < rows; ty += 3)
            for (int tx = 0; tx < cols; tx += 6) {
                uint32_t id = (uint32_t)(ty * 131 + tx * 7919) ^ 0x5ca7u;
                float start = hash01(id) * 0.45f, age = (p - start) / (1 - start);
                if (age >= 1) continue;
                int fall = age > 0 ? (int)(age * age * (float)rows * 1.3f) : 0, drift = age > 0 ? (int)((hash01(id + 1) - 0.5f) * 8 * age) : 0;
                for (int y = ty; y < ty + 3 && y < rows; y++)
                    for (int x = tx; x < tx + 6 && x < cols; x++) {
                        const Cell *c = grid_at(from, x, y);
                        int dx = x + drift, dy = y + fall;
                        if (c->glyph != ' ' && dx >= 0 && dx < cols && dy < rows) *grid_at(to, dx, dy) = *c;
                    }
            }
    }
    return 1;
}

// With reduce motion on, the entrances that move whole pictures give way to reveals in place of a like shape.
static Entrance still_entrance(Entrance how) {
    switch (how) {
    case ENTER_PUSH: return ENTER_WIPE;
    case ENTER_DOORS:
    case ENTER_ZOOM: return ENTER_RADIAL;
    case ENTER_INTERLACE: return ENTER_BLINDS;
    case ENTER_SWIRL: return ENTER_SPIRAL;
    case ENTER_SHATTER:
    case ENTER_GLITCH:
    case ENTER_KALEIDO: return ENTER_DISSOLVE;
    case ENTER_SLIDE: return ENTER_WIPE;
    case ENTER_CASCADE:
    case ENTER_TETRIS: return ENTER_MELT;
    case ENTER_RIPPLE: return ENTER_RADIAL;
    case ENTER_FLIP: return ENTER_BLINDS;
    default: return how;
    }
}

// Moves a color away from the ground by k: toward it below 1, past itself above 1.
static Rgb from_ground(Rgb c, Rgb ground, float k) {
    Rgbf a = rgbf(ground), b = rgbf(c);
    return rgb_from_f((Rgbf){a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k});
}

static void blend(Grid *from, Grid *to, Entrance how, int direction, float progress, float t, Rgb ground) {
    if (move_pictures(from, to, how, direction, saturate(progress))) return;
    if (how == ENTER_FADE) {
        // Through the bare ground: the old shot fades out, then the new one comes up.
        for (int i = 0; i < to->cols * to->rows; i++) {
            if (progress < 0.5f) {
                to->cells[i] = from->cells[i];
                to->cells[i].fg = from_ground(to->cells[i].fg, ground, 1 - progress * 2);
            } else {
                to->cells[i].fg = from_ground(to->cells[i].fg, ground, progress * 2 - 1);
            }
        }
        return;
    }
    int edged = how != ENTER_DISSOLVE && how != ENTER_DECODE && how != ENTER_CHECKER;
    const float *order = reveal_keys(how, direction, to);
    for (int row = 0; row < to->rows; row++)
        for (int col = 0; col < to->cols; col++) {
            float key = order ? order[row * to->cols + col] : reveal_key(how, direction, col, row, to);
            Cell *dst = grid_at(to, col, row);
            if (key > progress) {
                *dst = *grid_at(from, col, row);
            } else if (how == ENTER_DECODE && key > progress - 0.12f && dst->glyph != ' ') {
                // Cells about to settle flicker through random symbols first, like a signal locking on.
                uint32_t roll = hash_u32((uint32_t)(row * 131 + col) ^ (uint32_t)(t * 24.0f));
                dst->glyph = (uint8_t)SCRAMBLE[roll % (sizeof SCRAMBLE - 1)];
            } else if (how == ENTER_TYPEWRITER && key > progress - 0.01f) {
                // The cursor, just ahead of the text, in whichever of light or dark stands off the ground.
                int dark = ground.r + ground.g + ground.b < 384;
                dst->glyph = '_';
                dst->fg = dark ? (Rgb){230, 230, 230} : (Rgb){25, 25, 25};
            } else if (edged && key > progress - 0.035f && dst->glyph != ' ') {
                // The front of a sweep stands out as it passes: brighter on a dark ground, deeper on a light one.
                dst->fg = from_ground(dst->fg, ground, 1.6f);
            }
        }
}

void direction_draw(Direction *d, Frame *f, Grid *scratch) {
    if (!d->count) return;
    int i = 0;
    while (i + 1 < d->count && f->t >= d->shots[i + 1].start) i++;
    Shot *shot = &d->shots[i];
    float since = f->t - shot->start;
    if (i > 0 && shot->fade > 0 && since < shot->fade) {
        render_shot(d, &d->shots[i - 1], f, scratch);
        float glow = f->fx->glow;
        render_shot(d, shot, f, f->grid);
        float progress = smoothstep(0.0f, shot->fade, since);
        f->fx->glow = mixf(glow, f->fx->glow, progress);
        f->fx->bg = rgb_mix(d->palettes[d->shots[i - 1].palette]->bg, d->palettes[shot->palette]->bg, progress);
        // A glitch's tearing is a flicker as much as a motion, so reduce flashing calms it too.
        Entrance how = f->still || (f->calm && shot->entrance == ENTER_GLITCH) ? still_entrance(shot->entrance) : shot->entrance;
        blend(scratch, f->grid, how, shot->direction, progress * 1.05f, f->t, f->fx->bg);
    } else {
        render_shot(d, shot, f, f->grid);
        f->fx->bg = d->palettes[shot->palette]->bg;
    }
}
