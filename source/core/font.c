// SPDX-License-Identifier: GPL-3.0-only
#include "font.h"

const FontFace *font_face(int design_w, int cell_h, int wide) {
    for (int i = 0; i < font_face_count; i++) {
        const FontFace *f = &font_faces[i];
        if (f->design_w == design_w && f->cell_h == cell_h && (f->cell_w != f->design_w) == !!wide)
            return f;
    }
    return &font_faces[0];
}
