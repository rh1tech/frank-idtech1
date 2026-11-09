/*
 * Drawing for the firmware UI: rectangles, bevels and the 6x8 font, into
 * the hidden frame buffer. Everything drawn is marked dirty for
 * fui_present().
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fui.h"
#include "font6x8.h"
#include <string.h>

void fui_fill(int x, int y, int w, int h, uint8_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FUI_W) w = FUI_W - x;
    if (y + h > FUI_H) h = FUI_H - y;
    if (w <= 0 || h <= 0) return;
    uint8_t *p = fui_back() + y * FUI_W + x;
    for (int r = 0; r < h; r++, p += FUI_W) memset(p, c, (size_t)w);
    fui_dirty(x, y, w, h);
}

void fui_px(int x, int y, uint8_t c) {
    if ((unsigned)x >= FUI_W || (unsigned)y >= FUI_H) return;
    fui_back()[y * FUI_W + x] = c;
    fui_dirty(x, y, 1, 1);
}

static const uint8_t *glyph(char ch) {
    unsigned u = (unsigned char)ch;
    if (u < 32 || u > 126) u = '?';
    return font6x8[u - 32];
}

int fui_text_n(int x, int y, const char *s, int max_chars, uint8_t c) {
    uint8_t *fb = fui_back();
    const int x0 = x;
    for (int n = 0; *s && n < max_chars; s++, n++, x += FUI_FONT_W) {
        const uint8_t *g = glyph(*s);
        for (int r = 0; r < FUI_FONT_H; r++) {
            const int yy = y + r;
            if ((unsigned)yy >= FUI_H || !g[r]) continue;
            for (int k = 0; k < FUI_FONT_W; k++) {
                const int xx = x + k;
                if ((g[r] & (0x80 >> k)) && (unsigned)xx < FUI_W) fb[yy * FUI_W + xx] = c;
            }
        }
    }
    fui_dirty(x0, y, x - x0, FUI_FONT_H);
    return x;
}

int fui_text(int x, int y, const char *s, uint8_t c) {
    return fui_text_n(x, y, s, 1 << 30, c);
}

int fui_text_w(const char *s) {
    return (int)strlen(s) * FUI_FONT_W;
}

void fui_text_center(int x0, int x1, int y, const char *s, uint8_t c) {
    fui_text(x0 + (x1 - x0 - fui_text_w(s)) / 2, y, s, c);
}

void fui_text_right(int x1, int y, const char *s, uint8_t c) {
    fui_text(x1 - fui_text_w(s), y, s, c);
}

void fui_title(int x0, int y0, const char *s, int scale, int shadow, uint8_t row_base) {
    uint8_t *fb = fui_back();
    const int n = (int)strlen(s);
    for (int pass = shadow >= 0 ? 0 : 1; pass < 2; pass++) {
        const int off = pass ? 0 : 2;
        for (int i = 0; i < n; i++) {
            const uint8_t *g = glyph(s[i]);
            for (int r = 0; r < FUI_FONT_H; r++)
                for (int k = 0; k < FUI_FONT_W; k++) {
                    if (!(g[r] & (0x80 >> k))) continue;
                    const int x = x0 + (i * FUI_FONT_W + k) * scale + off;
                    const int y = y0 + r * scale + off;
                    for (int dy = 0; dy < scale; dy++) {
                        if ((unsigned)(y + dy) >= FUI_H || x < 0 || x + scale > FUI_W) continue;
                        memset(fb + (y + dy) * FUI_W + x,
                               pass ? (uint8_t)(row_base + r * scale + dy) : (uint8_t)shadow,
                               (size_t)scale);
                    }
                }
        }
    }
    fui_dirty(x0, y0, n * FUI_FONT_W * scale + 2, FUI_FONT_H * scale + 2);
}

void fui_bevel(int x, int y, int w, int h, bool raised, uint8_t lt, uint8_t dk) {
    const uint8_t a = raised ? lt : dk, b = raised ? dk : lt;
    fui_fill(x, y, w, 1, a);
    fui_fill(x, y, 1, h, a);
    fui_fill(x, y + h - 1, w, 1, b);
    fui_fill(x + w - 1, y, 1, h, b);
}
