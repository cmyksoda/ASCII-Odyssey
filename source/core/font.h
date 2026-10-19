// SPDX-License-Identifier: GPL-3.0-only
#ifndef FONT_H
#define FONT_H

#define FONT_FIRST 32
#define FONT_LAST 126
#define FONT_GLYPHS (FONT_LAST - FONT_FIRST + 1)

typedef struct {
    int cell_w, cell_h;
    // The 4:3 width this face imitates; 16:9 faces are their twin squeezed by 3/4.
    int design_w;
    int atlas_w, atlas_h, columns;
    const unsigned char *atlas;
    // Mean coverage of each glyph in 2 columns x 4 rows, top row first.
    const unsigned char *shape;
} FontFace;

extern const FontFace font_faces[];
extern const int font_face_count;

const FontFace *font_face(int design_w, int cell_h, int wide);

#endif
