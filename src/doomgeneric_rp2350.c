#include "doomgeneric.h"
#include "doomtype.h"
#include "board_config.h"
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "HDMI.h"
#include "psram_init.h"
#include "psram_allocator.h"
#include "sdcard.h"
#include "ff.h"
#include "ps2kbd_wrapper.h"
#include "ps2mouse_wrapper.h"
#include "usbhid_wrapper.h"
#include "doomkeys.h"
#include "m_argv.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static void draw_text_5x7(int x, int y, const char *text, pixel_t color);
static void fill_rect(int x, int y, int w, int h, pixel_t color);
static int text_width_5x7(const char *text, int scale);

static void fill_rect(int x, int y, int w, int h, pixel_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > DOOMGENERIC_RESX) w = DOOMGENERIC_RESX - x;
    if (y + h > DOOMGENERIC_RESY) h = DOOMGENERIC_RESY - y;
    if (w <= 0 || h <= 0) return;

    for (int yy = y; yy < y + h; ++yy) {
        memset(&DG_ScreenBuffer[yy * DOOMGENERIC_RESX + x], color, (size_t)w);
    }
}

// Game engine type for each IWAD
typedef enum {
    ENGINE_DOOM = 0,
    ENGINE_HERETIC = 1,
    ENGINE_HEXEN = 2,
    ENGINE_STRIFE = 3,
} game_engine_t;

typedef struct {
    const char *filename;
    const char *label;
    game_engine_t engine;
} iwad_entry_t;

static int max_label_width_5x7(const iwad_entry_t *const *entries, int entry_count, int scale) {
    int max_w = 0;
    for (int i = 0; i < entry_count; ++i) {
        const char *label = entries[i]->label;
        int w = text_width_5x7(label, scale);
        if (w > max_w) max_w = w;
    }
    return max_w;
}

static void render_iwad_menu_entries(const iwad_entry_t *const *available,
                                     int available_count,
                                     int selected,
                                     int scroll_offset,
                                     int max_visible,
                                     int menu_x,
                                     int menu_y,
                                     int line_h,
                                     int menu_w,
                                     int visible_h,
                                     int highlight_h) {
    // Clear menu area.
    fill_rect(menu_x - 2, menu_y - 2, menu_w + 4, visible_h + 4, 0);

    if (available_count <= 0) {
        draw_text_5x7(menu_x, menu_y, "NONE", 1);
        return;
    }

    int end = scroll_offset + max_visible;
    if (end > available_count) end = available_count;

    for (int i = scroll_offset; i < end; ++i) {
        const int y = menu_y + (i - scroll_offset) * line_h;
        const char *disp = available[i]->label;

        if (i == selected) {
            fill_rect(menu_x - 2, y - 1, menu_w + 4, highlight_h, 1);
            draw_text_5x7(menu_x, y, disp, 0);
        } else {
            draw_text_5x7(menu_x, y, disp, 1);
        }
    }

    // Scroll indicators
    if (scroll_offset > 0)
        draw_text_5x7(menu_x + menu_w + 6, menu_y, "^", 1);
    if (end < available_count)
        draw_text_5x7(menu_x + menu_w + 6, menu_y + (max_visible - 1) * line_h, "v", 1);
}

static const uint8_t *glyph_5x7(char ch) {
    // 5x7 glyphs, bits are MSB->LSB across 5 columns.
    static const uint8_t glyph_space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t glyph_dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    static const uint8_t glyph_comma[7] = {0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x08};
    static const uint8_t glyph_colon[7] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
    static const uint8_t glyph_hyphen[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static const uint8_t glyph_lparen[7] = {0x04, 0x08, 0x08, 0x08, 0x08, 0x08, 0x04};
    static const uint8_t glyph_rparen[7] = {0x04, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04};
    static const uint8_t glyph_slash[7] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00};

    static const uint8_t glyph_0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const uint8_t glyph_1[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t glyph_2[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    static const uint8_t glyph_3[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t glyph_4[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static const uint8_t glyph_5[7] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    static const uint8_t glyph_6[7] = {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    static const uint8_t glyph_7[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t glyph_8[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    static const uint8_t glyph_9[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};

    static const uint8_t glyph_a[7] = {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F};
    static const uint8_t glyph_b[7] = {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E};
    static const uint8_t glyph_c[7] = {0x00, 0x00, 0x0E, 0x11, 0x10, 0x11, 0x0E};
    static const uint8_t glyph_d[7] = {0x01, 0x01, 0x0D, 0x13, 0x11, 0x13, 0x0D};
    static const uint8_t glyph_e[7] = {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0F};
    static const uint8_t glyph_f[7] = {0x06, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t glyph_g[7] = {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E};
    static const uint8_t glyph_h[7] = {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11};
    static const uint8_t glyph_i[7] = {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t glyph_j[7] = {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C};
    static const uint8_t glyph_k[7] = {0x10, 0x10, 0x11, 0x12, 0x1C, 0x12, 0x11};
    static const uint8_t glyph_l[7] = {0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x06};
    static const uint8_t glyph_m[7] = {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15};
    static const uint8_t glyph_n[7] = {0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11};
    static const uint8_t glyph_o[7] = {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t glyph_p[7] = {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10};
    static const uint8_t glyph_q[7] = {0x00, 0x00, 0x0D, 0x13, 0x13, 0x0D, 0x01};
    static const uint8_t glyph_r[7] = {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10};
    static const uint8_t glyph_s[7] = {0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E};
    static const uint8_t glyph_t[7] = {0x04, 0x04, 0x1F, 0x04, 0x04, 0x04, 0x03};
    static const uint8_t glyph_u[7] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D};
    static const uint8_t glyph_v[7] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04};
    static const uint8_t glyph_w[7] = {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A};
    static const uint8_t glyph_x[7] = {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11};
    static const uint8_t glyph_y[7] = {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E};
    static const uint8_t glyph_z[7] = {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F};

    static const uint8_t glyph_A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t glyph_B[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static const uint8_t glyph_C[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t glyph_D[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    static const uint8_t glyph_E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t glyph_F[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t glyph_G[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
    static const uint8_t glyph_H[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t glyph_I[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    static const uint8_t glyph_J[7] = {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
    static const uint8_t glyph_K[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t glyph_L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t glyph_M[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t glyph_N[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t glyph_O[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t glyph_P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t glyph_Q[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    static const uint8_t glyph_R[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const uint8_t glyph_S[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t glyph_T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t glyph_U[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t glyph_V[7] = {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04};
    static const uint8_t glyph_W[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
    static const uint8_t glyph_X[7] = {0x11, 0x0A, 0x04, 0x04, 0x04, 0x0A, 0x11};
    static const uint8_t glyph_Y[7] = {0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t glyph_Z[7] = {0x1F, 0x02, 0x04, 0x08, 0x10, 0x10, 0x1F};

    int c = (unsigned char)ch;
    switch (c) {
        case ' ': return glyph_space;
        case '.': return glyph_dot;
        case ',': return glyph_comma;
        case ':': return glyph_colon;
        case '-': return glyph_hyphen;
        case '(': return glyph_lparen;
        case ')': return glyph_rparen;
        case '/': return glyph_slash;

        case '0': return glyph_0;
        case '1': return glyph_1;
        case '2': return glyph_2;
        case '3': return glyph_3;
        case '4': return glyph_4;
        case '5': return glyph_5;
        case '6': return glyph_6;
        case '7': return glyph_7;
        case '8': return glyph_8;
        case '9': return glyph_9;

        case 'a': return glyph_a;
        case 'b': return glyph_b;
        case 'c': return glyph_c;
        case 'd': return glyph_d;
        case 'e': return glyph_e;
        case 'f': return glyph_f;
        case 'g': return glyph_g;
        case 'h': return glyph_h;
        case 'i': return glyph_i;
        case 'j': return glyph_j;
        case 'k': return glyph_k;
        case 'l': return glyph_l;
        case 'm': return glyph_m;
        case 'n': return glyph_n;
        case 'o': return glyph_o;
        case 'p': return glyph_p;
        case 'q': return glyph_q;
        case 'r': return glyph_r;
        case 's': return glyph_s;
        case 't': return glyph_t;
        case 'u': return glyph_u;
        case 'v': return glyph_v;
        case 'w': return glyph_w;
        case 'x': return glyph_x;
        case 'y': return glyph_y;
        case 'z': return glyph_z;

        case 'A': return glyph_A;
        case 'B': return glyph_B;
        case 'C': return glyph_C;
        case 'D': return glyph_D;
        case 'E': return glyph_E;
        case 'F': return glyph_F;
        case 'G': return glyph_G;
        case 'H': return glyph_H;
        case 'I': return glyph_I;
        case 'J': return glyph_J;
        case 'K': return glyph_K;
        case 'L': return glyph_L;
        case 'M': return glyph_M;
        case 'N': return glyph_N;
        case 'O': return glyph_O;
        case 'P': return glyph_P;
        case 'Q': return glyph_Q;
        case 'R': return glyph_R;
        case 'S': return glyph_S;
        case 'T': return glyph_T;
        case 'U': return glyph_U;
        case 'V': return glyph_V;
        case 'W': return glyph_W;
        case 'X': return glyph_X;
        case 'Y': return glyph_Y;
        case 'Z': return glyph_Z;

        default: return glyph_space;
    }
}

static void draw_char_5x7(int x, int y, char ch, pixel_t color) {
    const uint8_t *rows = glyph_5x7(ch);
    for (int row = 0; row < 7; ++row) {
        int yy = y + row;
        if (yy < 0 || yy >= DOOMGENERIC_RESY) continue;
        uint8_t bits = rows[row];
        for (int col = 0; col < 5; ++col) {
            int xx = x + col;
            if (xx < 0 || xx >= DOOMGENERIC_RESX) continue;
            if (bits & (1u << (4 - col))) {
                DG_ScreenBuffer[yy * DOOMGENERIC_RESX + xx] = color;
            }
        }
    }
}

static void draw_char_5x7_scaled(int x, int y, char ch, pixel_t color, int scale, bool bold) {
    if (scale <= 1) {
        draw_char_5x7(x, y, ch, color);
        if (bold) draw_char_5x7(x + 1, y, ch, color);
        return;
    }

    const uint8_t *rows = glyph_5x7(ch);
    for (int row = 0; row < 7; ++row) {
        uint8_t bits = rows[row];
        for (int col = 0; col < 5; ++col) {
            if (!(bits & (1u << (4 - col)))) continue;
            const int px = x + col * scale;
            const int py = y + row * scale;
            fill_rect(px, py, scale, scale, color);
            if (bold) fill_rect(px + 1, py, scale, scale, color);
        }
    }
}

static void draw_text_5x7(int x, int y, const char *text, pixel_t color) {
    const int advance = 6;
    for (const char *p = text; *p; ++p) {
        draw_char_5x7(x, y, *p, color);
        x += advance;
    }
}

static void draw_text_5x7_scaled(int x, int y, const char *text, pixel_t color, int scale, bool bold) {
    const int advance = 6 * (scale <= 0 ? 1 : scale);
    if (scale <= 0) scale = 1;
    for (const char *p = text; *p; ++p) {
        draw_char_5x7_scaled(x, y, *p, color, scale, bold);
        x += advance;
    }
}

static int text_width_5x7(const char *text, int scale) {
    if (scale <= 0) scale = 1;
    int n = 0;
    for (const char *p = text; *p; ++p) n++;
    return n * 6 * scale;
}

static void draw_animated_background_border(uint32_t t_ms,
                                           int panel_x,
                                           int panel_y,
                                           int panel_w,
                                           int panel_h) {
    const int t = (int)(t_ms / 80);

    if (panel_x < 0) panel_x = 0;
    if (panel_y < 0) panel_y = 0;
    if (panel_w < 0) panel_w = 0;
    if (panel_h < 0) panel_h = 0;
    if (panel_x + panel_w > DOOMGENERIC_RESX) panel_w = DOOMGENERIC_RESX - panel_x;
    if (panel_y + panel_h > DOOMGENERIC_RESY) panel_h = DOOMGENERIC_RESY - panel_y;

    const int panel_x2 = panel_x + panel_w;
    const int panel_y2 = panel_y + panel_h;

    // Top strip.
    for (int y = 0; y < panel_y; ++y) {
        for (int x = 0; x < DOOMGENERIC_RESX; ++x) {
            const int bx = (x >> 3);
            const int by = (y >> 3);
            uint8_t v = (uint8_t)((bx + by + t) & 0x0F);
            v ^= (uint8_t)(((bx << 1) ^ (by + (t >> 1))) & 0x07);
            DG_ScreenBuffer[y * DOOMGENERIC_RESX + x] = (pixel_t)(2 + (v & 0x0F));
        }
    }

    // Bottom strip.
    for (int y = panel_y2; y < DOOMGENERIC_RESY; ++y) {
        for (int x = 0; x < DOOMGENERIC_RESX; ++x) {
            const int bx = (x >> 3);
            const int by = (y >> 3);
            uint8_t v = (uint8_t)((bx + by + t) & 0x0F);
            v ^= (uint8_t)(((bx << 1) ^ (by + (t >> 1))) & 0x07);
            DG_ScreenBuffer[y * DOOMGENERIC_RESX + x] = (pixel_t)(2 + (v & 0x0F));
        }
    }

    // Left/right strips.
    for (int y = panel_y; y < panel_y2; ++y) {
        for (int x = 0; x < panel_x; ++x) {
            const int bx = (x >> 3);
            const int by = (y >> 3);
            uint8_t v = (uint8_t)((bx + by + t) & 0x0F);
            v ^= (uint8_t)(((bx << 1) ^ (by + (t >> 1))) & 0x07);
            DG_ScreenBuffer[y * DOOMGENERIC_RESX + x] = (pixel_t)(2 + (v & 0x0F));
        }
        for (int x = panel_x2; x < DOOMGENERIC_RESX; ++x) {
            const int bx = (x >> 3);
            const int by = (y >> 3);
            uint8_t v = (uint8_t)((bx + by + t) & 0x0F);
            v ^= (uint8_t)(((bx << 1) ^ (by + (t >> 1))) & 0x07);
            DG_ScreenBuffer[y * DOOMGENERIC_RESX + x] = (pixel_t)(2 + (v & 0x0F));
        }
    }
}

static void fade_start_screen_bg_to_black(const uint32_t bg_pal[16], uint32_t title_hl_rgb, int steps, int step_delay_ms) {
    if (steps <= 0) return;
    if (step_delay_ms < 0) step_delay_ms = 0;

    for (int s = 0; s <= steps; ++s) {
        const int num = steps - s;
        const int den = steps;

        for (int i = 0; i < 16; ++i) {
            const uint32_t base = bg_pal[i];
            uint32_t r = (base >> 16) & 0xFF;
            uint32_t g = (base >> 8) & 0xFF;
            uint32_t b = base & 0xFF;
            r = (r * (uint32_t)num) / (uint32_t)den;
            g = (g * (uint32_t)num) / (uint32_t)den;
            b = (b * (uint32_t)num) / (uint32_t)den;
            graphics_set_palette(2 + i, (r << 16) | (g << 8) | b);
        }

        {
            const uint32_t base = title_hl_rgb;
            uint32_t r = (base >> 16) & 0xFF;
            uint32_t g = (base >> 8) & 0xFF;
            uint32_t b = base & 0xFF;
            r = (r * (uint32_t)num) / (uint32_t)den;
            g = (g * (uint32_t)num) / (uint32_t)den;
            b = (b * (uint32_t)num) / (uint32_t)den;
            graphics_set_palette(18, (r << 16) | (g << 8) | b);
        }

        if (step_delay_ms) sleep_ms((uint32_t)step_delay_ms);
    }
}

static int ascii_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static int ascii_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = ascii_tolower((unsigned char)*a);
        int cb = ascii_tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        ++a;
        ++b;
    }
    return ascii_tolower((unsigned char)*a) - ascii_tolower((unsigned char)*b);
}

static bool has_wad_extension(const char *name) {
    size_t n = strlen(name);
    if (n < 4) return false;
    return (ascii_tolower((unsigned char)name[n - 4]) == '.'
         && ascii_tolower((unsigned char)name[n - 3]) == 'w'
         && ascii_tolower((unsigned char)name[n - 2]) == 'a'
         && ascii_tolower((unsigned char)name[n - 1]) == 'd');
}

// All known IWADs across all supported games
typedef struct {
    const char *filename;
    game_engine_t engine;
} known_iwad_t;

static const known_iwad_t known_iwads[] = {
    // Doom IWADs
    {"doom2.wad",     ENGINE_DOOM},
    {"doom.wad",      ENGINE_DOOM},
    {"doom1.wad",     ENGINE_DOOM},
    {"plutonia.wad",  ENGINE_DOOM},
    {"tnt.wad",       ENGINE_DOOM},
    {"doom2f.wad",    ENGINE_DOOM},
    {"freedoom2.wad", ENGINE_DOOM},
    {"freedoom1.wad", ENGINE_DOOM},
    {"freedm.wad",    ENGINE_DOOM},
    {"chex.wad",      ENGINE_DOOM},
    {"hacx.wad",      ENGINE_DOOM},
    // Heretic IWADs
    {"heretic.wad",   ENGINE_HERETIC},
    {"heretic1.wad",  ENGINE_HERETIC},
    // Hexen IWADs
    {"hexen.wad",     ENGINE_HEXEN},
    {"hexdd.wad",     ENGINE_HEXEN},
    // Strife IWADs
    {"strife1.wad",   ENGINE_STRIFE},
    {"strife0.wad",   ENGINE_STRIFE},
};

// Number of known IWADs
#define NUM_KNOWN_IWADS (sizeof(known_iwads) / sizeof(known_iwads[0]))

static void print_available_wads_to_console(void) {
    printf("\n=== WAD scan (/ and /idtech1) ===\n");
    fflush(stdout);

    printf("Compatible IWAD filenames:\n");
    int compatible_found = 0;
    for (size_t i = 0; i < NUM_KNOWN_IWADS; ++i) {
        FILINFO info;
        if (f_stat(known_iwads[i].filename, &info) == FR_OK) {
            printf("  [FOUND] %s (root)\n", known_iwads[i].filename);
            compatible_found++;
        }
        char subpath[64];
        snprintf(subpath, sizeof(subpath), "idtech1/%s", known_iwads[i].filename);
        if (f_stat(subpath, &info) == FR_OK) {
            printf("  [FOUND] %s (idtech1/)\n", known_iwads[i].filename);
            compatible_found++;
        }
    }
    if (!compatible_found) {
        printf("  (none found by standard IWAD names)\n");
    }
    fflush(stdout);

    // Scan both root and /idtech1/ for other .wad files.
    static const char *const scan_dirs[] = { "/", "/idtech1" };
    printf("Other .wad files:\n");
    int other_count = 0;
    for (int d = 0; d < 2; ++d) {
        DIR dir;
        FILINFO fno;
        FRESULT fr = f_opendir(&dir, scan_dirs[d]);
        if (fr != FR_OK) continue;

        for (;;) {
            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK || fno.fname[0] == 0) break;

            if (fno.fattrib & AM_DIR) continue;
            if (!has_wad_extension(fno.fname)) continue;

            bool known = false;
            for (size_t i = 0; i < NUM_KNOWN_IWADS; ++i) {
                if (ascii_strcasecmp(fno.fname, known_iwads[i].filename) == 0) { known = true; break; }
            }
            if (known) continue;

            printf("  %s (%s)\n", fno.fname, scan_dirs[d]);
            other_count++;
        }
        f_closedir(&dir);
    }

    if (!other_count) {
        printf("  (none)\n");
    }

    printf("=== Press Enter to start ===\n\n");
    fflush(stdout);
}

// External variables from i_video.c (when CMAP256 is defined)
extern boolean palette_changed;

// Match struct color from i_video.h (Little Endian: b, g, r, a)
extern struct {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
} colors[256];

// Only available when USB CDC stdio is enabled (not in USB HID host builds).
#if PICO_STDIO_USB
#include "pico/stdio_usb.h"
#endif

// External stdio init for FatFS
extern void stdio_fatfs_init(void);

// Global FatFs object
FATFS fs;

void install_fault_handler(void);

void DG_Init() {
    install_fault_handler();

    // Initialize PSRAM (pin auto-detected based on chip package)
    uint psram_pin = get_psram_pin();
    psram_init(psram_pin);
    psram_set_sram_mode(0); // Use PSRAM

    // Allocate screen buffer in PSRAM
    DG_ScreenBuffer = (pixel_t*)psram_malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(pixel_t));
    if (!DG_ScreenBuffer) {
        panic("DG_Init: OOM for Screen Buffer");
    }

    // Clear screen buffer to black
    memset(DG_ScreenBuffer, 0, DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(pixel_t));

    // Initialize HDMI
    graphics_init(g_out_HDMI);
    graphics_set_res(320, 240);
    graphics_set_buffer((uint8_t*)DG_ScreenBuffer);

    // Mount SD Card
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        panic("Failed to mount SD card");
    }

    // Set current directory to root (required for relative paths)
    f_chdir("/");

    // Initialize stdio wrapper for FatFS
    stdio_fatfs_init();

    // Initialize PS/2 Keyboard
    ps2kbd_init();

    // Initialize PS/2 Mouse
    ps2mouse_wrapper_init();

    // Initialize USB HID (keyboard/mouse) if enabled
    usbhid_wrapper_init();
}

void DG_StartScreen(void) {
    // Solid black background using palette index 0.
    graphics_set_palette(0, 0x000000);
    graphics_set_palette(1, 0xFFFFFF);
    // Reserved for the loading window text/border (keep bright even after we dim UI text).
    graphics_set_palette(20, 0xFFFFFF);

    // Background palette (2..17): brighter blue theme.
    static const uint32_t bg_pal[16] = {
        0x020308, 0x040610, 0x060918, 0x080C20,
        0x0A0F28, 0x0C1230, 0x0E1538, 0x101840,
        0x121B48, 0x141E50, 0x162158, 0x182460,
        0x1A2768, 0x1C2A70, 0x1E2D78, 0x203080,
    };
    for (int i = 0; i < 16; ++i) {
        graphics_set_palette(2 + i, bg_pal[i]);
    }

    // Title highlight: 20% brighter than the brightest background green.
    uint32_t title_hl_rgb;
    {
        const uint32_t base = bg_pal[15];
        uint32_t r = (base >> 16) & 0xFF;
        uint32_t g = (base >> 8) & 0xFF;
        uint32_t b = base & 0xFF;
        r = (r * 6) / 5;
        g = (g * 6) / 5;
        b = (b * 6) / 5;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        title_hl_rgb = (r << 16) | (g << 8) | b;
        graphics_set_palette(18, title_hl_rgb);
    }
    memset(DG_ScreenBuffer, 0, DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(pixel_t));

    // All IWADs for selection menu (Doom + Heretic).
    static const iwad_entry_t game_iwads[] = {
        {"doom2.wad",    "DOOM II (doom2.wad)",            ENGINE_DOOM},
        {"doom.wad",     "DOOM (doom.wad)",                ENGINE_DOOM},
        {"doom1.wad",    "DOOM SHAREWARE (doom1.wad)",     ENGINE_DOOM},
        {"plutonia.wad", "FINAL DOOM: PLUTONIA",           ENGINE_DOOM},
        {"tnt.wad",      "FINAL DOOM: TNT (tnt.wad)",     ENGINE_DOOM},
        {"freedoom2.wad","FREEDOOM PHASE 2",               ENGINE_DOOM},
        {"freedoom1.wad","FREEDOOM PHASE 1",               ENGINE_DOOM},
        {"freedm.wad",   "FREEDM (freedm.wad)",           ENGINE_DOOM},
        {"chex.wad",     "CHEX QUEST (chex.wad)",          ENGINE_DOOM},
        {"hacx.wad",     "HACX (hacx.wad)",                ENGINE_DOOM},
        {"heretic.wad",  "HERETIC (heretic.wad)",          ENGINE_HERETIC},
        {"heretic1.wad", "HERETIC SHAREWARE",              ENGINE_HERETIC},
        {"hexen.wad",    "HEXEN (hexen.wad)",              ENGINE_HEXEN},
        {"hexdd.wad",    "HEXEN: DEATHKINGS",              ENGINE_HEXEN},
        {"strife1.wad",  "STRIFE (strife1.wad)",           ENGINE_STRIFE},
        {"strife0.wad",  "STRIFE DEMO (strife0.wad)",      ENGINE_STRIFE},
    };

    const int panel_x = 16;
    const int panel_y = 16;
    const int panel_w = DOOMGENERIC_RESX - 32;
    const int panel_h = DOOMGENERIC_RESY - 32;

    const int menu_x = panel_x + 4;
    const int menu_y = panel_y + 56;
    const int line_h = 10;
    const int highlight_h = 9;

    // Build list of available IWADs.
    // The game engine searches in "idtech1" subdir (see d_iwad.c BuildIWADDirList),
    // so check both root and /idtech1/ on the SD card.
    const iwad_entry_t *available[sizeof(game_iwads) / sizeof(game_iwads[0])];
    int available_count = 0;
    for (size_t i = 0; i < sizeof(game_iwads) / sizeof(game_iwads[0]); ++i) {
        FILINFO info;
        if (f_stat(game_iwads[i].filename, &info) == FR_OK) {
            available[available_count++] = &game_iwads[i];
            continue;
        }
        // Also check /idtech1/ subdirectory.
        char subpath[64];
        snprintf(subpath, sizeof(subpath), "idtech1/%s", game_iwads[i].filename);
        if (f_stat(subpath, &info) == FR_OK) {
            available[available_count++] = &game_iwads[i];
        }
    }

    const int bottom_y0 = panel_y + panel_h - 32;
    const int max_visible = (bottom_y0 - menu_y - 4) / line_h;  // fit between menu and status text
    const int visible_h = max_visible * line_h;
    int scroll_offset = 0;
    int selected = 0;

#ifndef FRANK_VERSION
#define FRANK_VERSION "?"
#endif

    const char *title_left = "FRANK IDTECH1";
    char title_right[96];
    snprintf(title_right, sizeof(title_right), " by Mikhail Matveev v%s", FRANK_VERSION);

    // Print WAD scan after ~3s or as soon as USB CDC is connected.
    const uint32_t start_ms = to_ms_since_boot(get_absolute_time());
    bool printed = false;
    bool printed_while_disconnected = false;

    const int title_scale = 1;
    const int title_left_w = text_width_5x7(title_left, title_scale);
    const int title_right_w = text_width_5x7(title_right, title_scale);
    const int title_w = title_left_w + title_right_w;
    const int title_x = (DOOMGENERIC_RESX - title_w) / 2;

    // Status lines
#ifndef DBOARD_VARIANT
#if defined(BOARD_M2)
#define DBOARD_VARIANT "M2"
#else
#define DBOARD_VARIANT "M1"
#endif
#endif
#ifndef DPSRAM_SPEED
#define DPSRAM_SPEED PSRAM_MAX_FREQ_MHZ
#endif

#ifndef DCPU_SPEED
#define DCPU_SPEED CPU_CLOCK_MHZ
#endif

    const char *cfg = DBOARD_VARIANT;
    const uint32_t cpu_mhz = (uint32_t)DCPU_SPEED;
    const uint32_t psram_cs = get_psram_pin();
    char status1[96];
    char status2[96];
    snprintf(status1, sizeof(status1), "Up/Down: select, Enter: confirm");
    snprintf(status2, sizeof(status2), "%s, FREQ: %lu MHz, PSRAM: %d MHz, CS: %lu",
             cfg,
             (unsigned long)cpu_mhz,
             (int)DPSRAM_SPEED,
             (unsigned long)psram_cs);
    const char *status3 = "https://github.com/rh1tech/frank-idtech1";

    // Compute menu width from available labels.
    const int menu_w = (available_count > 0 ? max_label_width_5x7(available, available_count, 1) : (6 * 4));

    draw_animated_background_border(to_ms_since_boot(get_absolute_time()), panel_x, panel_y, panel_w, panel_h);
    fill_rect(panel_x, panel_y, panel_w, panel_h, 0);
    // Highlight "FRANK HERETIC" with green background and black text.
    const int title_y = panel_y + 10;
    fill_rect(title_x - 2, title_y - 2, title_left_w + 4, 7 * title_scale + 4, 18);
    draw_text_5x7_scaled(title_x, title_y, title_left, 0, title_scale, false);
    draw_text_5x7_scaled(title_x + title_left_w, title_y, title_right, 1, title_scale, false);

    draw_text_5x7(menu_x, menu_y - 14, "Select WAD:", 1);
    draw_text_5x7(menu_x, bottom_y0 + 0, status1, 1);
    draw_text_5x7(menu_x, bottom_y0 + 10, status2, 1);
    draw_text_5x7(menu_x, bottom_y0 + 20, status3, 1);
    render_iwad_menu_entries(available, available_count, selected, scroll_offset, max_visible, menu_x, menu_y, line_h, menu_w, visible_h, highlight_h);

    int prev_selected = selected;

    // Wait for Enter.
    while (true) {
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        draw_animated_background_border(now_ms, panel_x, panel_y, panel_w, panel_h);

        if (prev_selected != selected) {
            // Adjust scroll to keep selection visible
            if (selected < scroll_offset) scroll_offset = selected;
            if (selected >= scroll_offset + max_visible) scroll_offset = selected - max_visible + 1;
            render_iwad_menu_entries(available, available_count, selected, scroll_offset, max_visible, menu_x, menu_y, line_h, menu_w, visible_h, highlight_h);
            prev_selected = selected;
        }

        bool usb_connected = true;
#if PICO_STDIO_USB
        usb_connected = stdio_usb_connected();
#endif

        if (!printed) {
            const uint32_t elapsed_ms = to_ms_since_boot(get_absolute_time()) - start_ms;
            if (usb_connected || elapsed_ms >= 3000) {
                print_available_wads_to_console();
                printed = true;
                printed_while_disconnected = !usb_connected;
            }
        } else if (printed_while_disconnected && usb_connected) {
            print_available_wads_to_console();
            printed_while_disconnected = false;
        }

        int pressed = 0;
        unsigned char key = 0;
        while (DG_GetKey(&pressed, &key)) {
            if (pressed && key == KEY_ENTER && available_count > 0) {
                // Dim existing UI text.
                graphics_set_palette(1, 0x404040);

                // Title left was previously black-on-green; redraw it using the (now grey) text color.
                draw_text_5x7_scaled(title_x, title_y, title_left, 1, title_scale, false);

                // Remove selection inversion.
                render_iwad_menu_entries(available, available_count, -1, scroll_offset, max_visible, menu_x, menu_y, line_h, menu_w, visible_h, highlight_h);

                // Fade out the start-screen background.
                fade_start_screen_bg_to_black(bg_pal, title_hl_rgb, 10, 20);

                char msg[96];
                snprintf(msg, sizeof(msg), "Loading WAD: %s...", available[selected]->filename);

                const int pad_x = 10;
                const int pad_y = 8;
                const int msg_w = text_width_5x7(msg, 1);
                int win_w = msg_w + pad_x * 2;
                if (win_w > DOOMGENERIC_RESX - 40) win_w = DOOMGENERIC_RESX - 40;
                const int win_h = 7 + pad_y * 2;
                const int win_x = (DOOMGENERIC_RESX - win_w) / 2;
                const int win_y = (DOOMGENERIC_RESY - win_h) / 2;

                fill_rect(win_x, win_y, win_w, win_h, 0);
                // Border
                fill_rect(win_x, win_y, win_w, 1, 20);
                fill_rect(win_x, win_y + win_h - 1, win_w, 1, 20);
                fill_rect(win_x, win_y, 1, win_h, 20);
                fill_rect(win_x + win_w - 1, win_y, 1, win_h, 20);

                draw_text_5x7(win_x + pad_x, win_y + pad_y, msg, 20);

                // Set the active game engine based on the selected WAD
                extern int active_game;
                active_game = (int)available[selected]->engine;

                static char *new_argv[4];
                if (available[selected]->engine == ENGINE_DOOM) {
                    new_argv[0] = (char *)"doom";
                } else if (available[selected]->engine == ENGINE_HEXEN) {
                    new_argv[0] = (char *)"hexen";
                } else if (available[selected]->engine == ENGINE_STRIFE) {
                    new_argv[0] = (char *)"strife";
                } else {
                    new_argv[0] = (char *)"heretic";
                }
                new_argv[1] = (char *)"-iwad";
                new_argv[2] = (char *)available[selected]->filename;
                new_argv[3] = NULL;
                myargc = 3;
                myargv = new_argv;
                return;
            }

            if (!pressed || available_count <= 0) continue;

            if (key == KEY_UPARROW || key == 'w' || key == 'W') {
                selected = (selected - 1 + available_count) % available_count;
            } else if (key == KEY_DOWNARROW || key == 's' || key == 'S') {
                selected = (selected + 1) % available_count;
            }
        }
        sleep_ms(33);
    }
}

extern volatile uint32_t hdmi_irq_count;

void DG_DrawFrame() {
    if (palette_changed) {
        for (int i = 0; i < 256; i++) {
            uint32_t color = (colors[i].r << 16) | (colors[i].g << 8) | colors[i].b;
            graphics_set_palette(i, color);
        }
        palette_changed = false;
    }
}

void DG_SleepMs(uint32_t ms) {
    sleep_ms(ms);
}

uint32_t DG_GetTicksMs() {
    return to_ms_since_boot(get_absolute_time());
}

int DG_GetKey(int* pressed, unsigned char* key) {
    ps2kbd_tick();
    ps2mouse_wrapper_tick();  // Process PS/2 mouse events
    // Note: usbhid_wrapper_tick() is called from I_StartTic() during gameplay

    // Check PS/2 keyboard first
    if (ps2kbd_get_key(pressed, key)) {
        return 1;
    }

    // Then check USB HID keyboard directly (for pre-game screens like WAD selection)
    if (usbhid_wrapper_get_key(pressed, key)) {
        return 1;
    }

    return 0;
}

void DG_SetWindowTitle(const char * title) {
}

// Hard fault handler - must be before DG_Init
#include "hardware/exception.h"

#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#define CRASH_MAGIC 0xDEAD0FAD

static void hard_fault_handler(void) {
    uint32_t *stack;
    __asm volatile("mrs %0, msp" : "=r"(stack));
    volatile uint32_t *cfsr = (volatile uint32_t *)0xE000ED28;

    // Save crash info to watchdog scratch registers (survive reboot)
    watchdog_hw->scratch[0] = CRASH_MAGIC;
    watchdog_hw->scratch[1] = stack[6];  // PC
    watchdog_hw->scratch[2] = stack[5];  // LR
    watchdog_hw->scratch[3] = *cfsr;     // CFSR
    watchdog_hw->scratch[4] = stack[0];  // R0
    watchdog_hw->scratch[5] = stack[1];  // R1

    // Force immediate reboot
    watchdog_reboot(0, 0, 0);
    while(1);
}

void install_fault_handler(void) {
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, hard_fault_handler);
}

// Call early in main to check for crash report from previous boot
void check_crash_report(void) {
    if (watchdog_hw->scratch[0] == CRASH_MAGIC) {
        printf("\n*** CRASH REPORT FROM PREVIOUS BOOT ***\n");
        printf("PC=0x%08lx  LR=0x%08lx\n",
               (unsigned long)watchdog_hw->scratch[1],
               (unsigned long)watchdog_hw->scratch[2]);
        printf("CFSR=0x%08lx  R0=0x%08lx  R1=0x%08lx\n",
               (unsigned long)watchdog_hw->scratch[3],
               (unsigned long)watchdog_hw->scratch[4],
               (unsigned long)watchdog_hw->scratch[5]);
        // Clear so we don't print again
        watchdog_hw->scratch[0] = 0;
        printf("Use: arm-none-eabi-addr2line -e frank-idtech1.elf 0x%08lx\n",
               (unsigned long)watchdog_hw->scratch[1]);
        printf("***\n\n");
    }
}

// I_System implementations

void I_Error(const char *error, ...) {
    va_list argptr;
    va_start(argptr, error);
    vprintf(error, argptr);
    va_end(argptr);
    printf("\n");
    while(1) tight_loop_contents();
}

void *I_Realloc(void *ptr, size_t size) {
    void *new_ptr = psram_realloc(ptr, size);
    if (size != 0 && new_ptr == NULL) {
        I_Error("I_Realloc: failed on reallocation of %zu bytes", size);
    }
    return new_ptr;
}

void I_Quit(void) {
    printf("I_Quit: rebooting to WAD selector...\n");
    // Clean reboot back to the start screen
    watchdog_reboot(0, 0, 10);  // Reboot in 10ms
    while(1) tight_loop_contents();
}

byte *I_ZoneBase(int *size) {
    // 6MB PERM pool: try 5MB zone first, fall back
    *size = 5 * 1024 * 1024; // 5MB PSRAM for zone (Strife+voices needs more)
    void *ptr = psram_malloc(*size);

    if (!ptr) {
        *size = 4 * 1024 * 1024;
        ptr = psram_malloc(*size);
    }
    if (!ptr) {
        *size = 3 * 1024 * 1024;
        ptr = psram_malloc(*size);
    }
    return (byte *)ptr;
}

void I_AtExit(void (*func)(void), boolean run_on_error) {
}

void I_PrintBanner(const char *msg) {
    printf("%s\n", msg);
}

void I_PrintDivider(void) {
    printf("------------------------------------------------\n");
}

void I_PrintStartupBanner(const char *gamedescription) {
    I_PrintDivider();
    printf("%s\n", gamedescription);
    I_PrintDivider();
}

boolean I_ConsoleStdout(void) {
    return true;
}

// I_Init implementation
void I_InitGraphics(void);
void I_InitTimer(void);

void I_Init(void) {
    I_InitTimer();
    I_InitGraphics();
}

// Missing I_ functions

void I_InitJoystick(void) {}
void I_BindJoystickVariables(void) {}
void I_Tactile(int on, int off, int total) {}

boolean I_GetMemoryValue(unsigned int offset, void *value, int size)
{
    return false;
}

