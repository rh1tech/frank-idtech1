/*
 * The welcome screen: the boot shell of frank-quake2 / frank-blood in Doom
 * colours. A riveted metal panel with the title, the WAD list, the board's
 * clocks, the copyrights and the repository URL, over embers rising in the
 * margins. Errors get a red title bar and a hint.
 *
 * All drawing goes to the hidden buffer and is shown at vblank
 * (fui_present), one frame per vblank: the list and the embers never
 * flicker. The engine's first frame flips in with its own palette, so the
 * welcome stays up while the WAD loads.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fui.h"
#include "doomkeys.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/structs/qmi.h"
#include <stdio.h>
#include <string.h>

#ifndef FRANK_VERSION
#define FRANK_VERSION "?"
#endif
#if defined(BOARD_Z0)
#define BOARD_NAME "Z0"
#elif defined(BOARD_M2)
#define BOARD_NAME "M2"
#else
#define BOARD_NAME "M1"
#endif

/* palette */
enum {
    C_BG = 0, C_TEXT, C_HI, C_ERR, C_DIM, C_PANEL, C_BEVEL_LT, C_BEVEL, C_BEVEL_DK, C_ERRBAR,
    C_WHITE, C_EMBER0, C_EMBER1, C_EMBER2, C_EMBER3, C_SHADOW, C_SEL, C_SEL_LT, C_SEL_DK,
    C_SEL_TAG, C_TITLE = 32, /* 32 rows */
};

/* layout (frank-quake2's, 320x240) */
#define PX0 24
#define PY0 16
#define PX1 296
#define PY1 224
#define BEVEL 4
#define TX0 34
#define TX1 286
#define TITLE_SCALE 4
#define TITLE_Y 26
#define SUB_Y 63
#define SEP1_Y 84
#define LIST_Y 90
#define ROW_H 9
#define LIST_ROWS 8
#define HINT_Y 168
#define SEP2_Y 180
#define FOOT_Y 185

static void palette(void) {
    static const uint32_t base[] = {
        [C_BG] = 0x000000,      [C_TEXT] = 0xC8C8C8,     [C_HI] = 0xFFB800,
        [C_ERR] = 0xFF3C28,     [C_DIM] = 0x808088,      [C_PANEL] = 0x121214,
        [C_BEVEL_LT] = 0xA0A0A8, [C_BEVEL] = 0x606068,   [C_BEVEL_DK] = 0x242428,
        [C_ERRBAR] = 0x8C0A06,  [C_WHITE] = 0xFFFFFF,    [C_EMBER0] = 0x5A0A00,
        [C_EMBER1] = 0xAA2800,  [C_EMBER2] = 0xF06E0A,   [C_EMBER3] = 0xFFDC50,
        [C_SHADOW] = 0x060404,  [C_SEL] = 0x960000,      [C_SEL_LT] = 0xDC3C28,
        [C_SEL_DK] = 0x3C0000,  [C_SEL_TAG] = 0xFFC8AA,
    };
    for (unsigned i = 0; i < sizeof(base) / sizeof(base[0]); i++) fui_pal((uint8_t)i, base[i]);

    /* the DOOM logo: yellow to orange, a bright rim, red down to brown */
    for (int r = 0; r < 32; r++) {
        int R, G, B;
        if (r < 13) {
            R = 255 - r / 2;
            G = 232 - r * 7;
            B = 120 - r * 8;
        } else if (r < 15) {
            R = 255; G = 240; B = 190;
        } else {
            const int k = r - 15;
            R = 225 - k * 9;
            G = 60 - k * 4;
            B = 10 - k / 2;
        }
        if (R < 0) R = 0;
        if (G < 0) G = 0;
        if (B < 0) B = 0;
        fui_pal((uint8_t)(C_TITLE + r), (uint32_t)(R << 16 | G << 8 | B));
    }
}

/* ---- embers in the side margins ---- */

#define N_EMBERS 32
#define TAIL 6

typedef struct {
    int16_t x, y4;     /* y in quarter pixels: the head */
    uint8_t speed;     /* quarter pixels per frame */
    int16_t drawn_x, drawn_y;
    bool drawn;
} ember_t;

static ember_t embers[N_EMBERS];
static uint32_t rng = 0x1234567u;

static uint32_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void ember_spawn(ember_t *e, bool anywhere) {
    const int margin = PX0 - 6; /* 2 .. PX0-4 from either edge */
    const int off = 2 + (int)(rnd() % (unsigned)margin);
    e->x = (int16_t)((rnd() & 1) ? off : FUI_W - 1 - off);
    e->y4 = (int16_t)((anywhere ? (int)(rnd() % FUI_H) : FUI_H + (int)(rnd() % 60)) * 4);
    e->speed = (uint8_t)(2 + rnd() % 5);
}

static void ember_paint(int x, int y, bool erase) {
    static const uint8_t tail[TAIL] = {C_EMBER3, C_EMBER2, C_EMBER2, C_EMBER1, C_EMBER1, C_EMBER0};
    for (int i = 0; i < TAIL; i++) fui_px(x, y + i, erase ? C_BG : tail[i]);
}

static void embers_init(void) {
    for (int i = 0; i < N_EMBERS; i++) {
        ember_spawn(&embers[i], true);
        embers[i].drawn = false;
    }
}

static void embers_step(void) {
    for (int i = 0; i < N_EMBERS; i++) {
        ember_t *e = &embers[i];
        e->y4 = (int16_t)(e->y4 - e->speed);
        if (e->y4 / 4 + TAIL < 0) ember_spawn(e, false);
        /* now and then a sideways flicker of the flame */
        if ((rnd() & 63) == 0) {
            const int nx = e->x + ((rnd() & 1) ? 1 : -1);
            if (nx >= 1 && nx < PX0 - 2) e->x = (int16_t)nx;
            else if (nx > PX1 + 1 && nx < FUI_W - 1) e->x = (int16_t)nx;
        }
        const int y = e->y4 / 4;
        if (e->drawn && e->drawn_x == e->x && e->drawn_y == y) continue;
        if (e->drawn) ember_paint(e->drawn_x, e->drawn_y, true);
        ember_paint(e->x, y, false);
        e->drawn_x = e->x;
        e->drawn_y = (int16_t)y;
        e->drawn = true;
    }
}

static void embers_clear(void) {
    for (int i = 0; i < N_EMBERS; i++)
        if (embers[i].drawn) {
            ember_paint(embers[i].drawn_x, embers[i].drawn_y, true);
            embers[i].drawn = false;
        }
}

/* ---- the panel ---- */

static void separator(int y) {
    fui_fill(TX0, y, TX1 - TX0, 1, C_BEVEL_DK);
    fui_fill(TX0, y + 1, TX1 - TX0, 1, C_BEVEL);
}

static void rivet(int x, int y) {
    fui_fill(x, y, 3, 3, C_BEVEL_DK);
    fui_px(x, y, C_BEVEL_LT);
    fui_px(x + 1, y, C_BEVEL_LT);
    fui_px(x, y + 1, C_BEVEL_LT);
}

static void panel(void) {
    fui_fill(PX0, PY0, PX1 - PX0, PY1 - PY0, C_BEVEL);
    fui_fill(PX0, PY0, PX1 - PX0, 1, C_BEVEL_LT); /* lit from the top left */
    fui_fill(PX0, PY0, 1, PY1 - PY0, C_BEVEL_LT);
    fui_fill(PX0, PY1 - 1, PX1 - PX0, 1, C_BEVEL_DK);
    fui_fill(PX1 - 1, PY0, 1, PY1 - PY0, C_BEVEL_DK);
    fui_fill(PX0 + BEVEL - 1, PY0 + BEVEL - 1, PX1 - PX0 - 2 * BEVEL + 2, PY1 - PY0 - 2 * BEVEL + 2,
             C_BEVEL_DK);
    fui_fill(PX0 + BEVEL, PY0 + BEVEL, PX1 - PX0 - 2 * BEVEL, PY1 - PY0 - 2 * BEVEL, C_PANEL);
    rivet(PX0 + BEVEL + 2, PY0 + BEVEL + 2);
    rivet(PX1 - BEVEL - 5, PY0 + BEVEL + 2);
    rivet(PX0 + BEVEL + 2, PY1 - BEVEL - 5);
    rivet(PX1 - BEVEL - 5, PY1 - BEVEL - 5);
}

static void footer(const char *hint) {
    char line[64];
    if (hint) {
        fui_text_center(TX0, TX1, FOOT_Y, hint, C_HI);
    } else {
        const unsigned sys = (unsigned)(clock_get_hz(clk_sys) / 1000000);
        const unsigned m1 = (qmi_hw->m[1].timing & QMI_M1_TIMING_CLKDIV_BITS) >> QMI_M1_TIMING_CLKDIV_LSB;
        const unsigned m0 = (qmi_hw->m[0].timing & QMI_M0_TIMING_CLKDIV_BITS) >> QMI_M0_TIMING_CLKDIV_LSB;
        snprintf(line, sizeof(line), "%s  CPU %u  PSRAM %u  FLASH %u  %s", BOARD_NAME, sys,
                 m1 ? sys / m1 : 0, m0 ? sys / m0 : 0,
                 fui_cfg.audio == FUI_AUDIO_HDMI ? "HDMI" : "I2S");
        fui_text_center(TX0, TX1, FOOT_Y, line, C_DIM);
    }
    fui_text_center(TX0, TX1, FOOT_Y + 11, "Port (c) 2026 Mikhail Matveev", C_TEXT);
    fui_text_center(TX0, TX1, FOOT_Y + 22, "github.com/rh1tech/frank-idtech1", C_HI);
}

static void screen(const char *hint) {
    uint8_t *fb = fui_back();
    memset(fb, C_BG, FUI_W * FUI_H);
    fui_dirty_all();
    palette();
    panel();
    fui_title((FUI_W - 7 * FUI_FONT_W * TITLE_SCALE) / 2, TITLE_Y, "IDTECH1", TITLE_SCALE, C_SHADOW,
              C_TITLE);
    fui_text_center(TX0, TX1, SUB_Y, "for RP2350  -  frank-idtech1 " FRANK_VERSION, C_TEXT);
    fui_text_center(TX0, TX1, SUB_Y + 10, "Doom, Heretic, Hexen, Strife (c) 1993-96", C_DIM);
    separator(SEP1_Y);
    separator(SEP2_Y);
    footer(hint);
    embers_init();
}

/* ---- the WAD list ---- */

static void draw_row(const fui_wad_t *w, int row, bool selected, int arrow) {
    const int y = LIST_Y + row * ROW_H;
    const int x0 = TX0 - 2, x1 = TX1 + 2;
    if (selected) {
        fui_fill(x0, y, x1 - x0, ROW_H, C_SEL);
        fui_bevel(x0, y, x1 - x0, ROW_H, false, C_SEL_LT, C_SEL_DK);
    } else {
        fui_fill(x0, y, x1 - x0, ROW_H, C_PANEL);
    }
    if (w) {
        fui_text_n(TX0 + 2, y + 1, w->label, 28, selected ? C_WHITE : C_TEXT);
        fui_text_right(TX1 - 10, y + 1, w->file, selected ? C_SEL_TAG : C_DIM);
    }
    if (arrow) fui_text(TX1 - 6, y + 1, arrow < 0 ? "^" : "v", selected ? C_WHITE : C_HI);
}

static void draw_list(const fui_wad_t *list, int n, int sel, int top) {
    for (int r = 0; r < LIST_ROWS; r++) {
        const int i = top + r;
        int arrow = 0;
        if (r == 0 && top > 0) arrow = -1;
        if (r == LIST_ROWS - 1 && top + LIST_ROWS < n) arrow = 1;
        draw_row(i < n ? &list[i] : NULL, r, i == sel, arrow);
    }
}

static void hint(const char *s, uint8_t c) {
    fui_fill(TX0, HINT_Y, TX1 - TX0, FUI_FONT_H, C_PANEL);
    fui_text_center(TX0, TX1, HINT_Y, s, c);
}

int fui_shell_select(const fui_wad_t *list, int n, int autostart, const char *notice) {
    screen(NULL);
    int sel = autostart >= 0 ? autostart : 0, top = 0;
    if (sel >= LIST_ROWS) top = sel - LIST_ROWS + 1;
    draw_list(list, n, sel, top);
    if (notice) {
        hint(notice, C_ERR);
    } else {
        hint("Up/Down: select    Enter: start", C_DIM);
    }
    fui_key_flush();

    /* "start at boot": that WAD, unless a key comes within 3 s */
    int countdown = autostart >= 0 ? 3 * 60 + 1 : 0;
    int frames = 0;

    for (;;) {
        embers_step();
        fui_present(); /* one frame per vblank */
        if (++frames == 90) fui_cfg_boot_ok(); /* 1.5 s up: the clocks work */

        bool go = false;
        if (countdown > 0) {
            if (--countdown % 60 == 0) {
                char msg[48];
                snprintf(msg, sizeof(msg), "Starting %s in %d (any key: list)", list[sel].file,
                         countdown / 60 + 1);
                if (countdown) hint(msg, C_HI);
                else go = true;
            }
        }

        int k;
        while (!go && (k = fui_key(0)) >= 0) {
            const int old_sel = sel, old_top = top;
            if (countdown > 0) {  /* any key stops the countdown */
                countdown = 0;
                hint("Up/Down: select    Enter: start", C_DIM);
                continue;
            }
            if (k == KEY_UPARROW || k == 'w') sel = (sel + n - 1) % n;
            else if (k == KEY_DOWNARROW || k == 's') sel = (sel + 1) % n;
            else if (k == KEY_PGUP || k == KEY_HOME) sel = 0;
            else if (k == KEY_PGDN || k == KEY_END) sel = n - 1;
            else if (k == KEY_ENTER || k == KEY_USE) go = true;
            if (go) break;
            if (sel < top) top = sel;
            if (sel >= top + LIST_ROWS) top = sel - LIST_ROWS + 1;
            if (top != old_top) {
                draw_list(list, n, sel, top);
            } else if (sel != old_sel) {
                const int a0 = (old_sel - top == 0 && top > 0) ? -1
                             : (old_sel - top == LIST_ROWS - 1 && top + LIST_ROWS < n) ? 1 : 0;
                const int a1 = (sel - top == 0 && top > 0) ? -1
                             : (sel - top == LIST_ROWS - 1 && top + LIST_ROWS < n) ? 1 : 0;
                draw_row(&list[old_sel], old_sel - top, false, a0);
                draw_row(&list[sel], sel - top, true, a1);
            }
        }
        if (go) {
            char msg[48];
            snprintf(msg, sizeof(msg), "Loading %s...", list[sel].file);
            hint(msg, C_HI);
            embers_clear();
            fui_present();
            fui_cfg_boot_ok();
            return sel;
        }
    }
}

void fui_shell_error(const char *title, const char *line1, const char *line2, const char *hint_text) {
    screen(hint_text);
    fui_fill(TX0, LIST_Y, TX1 - TX0, 12, C_ERRBAR);
    fui_fill(TX0, LIST_Y, TX1 - TX0, 1, C_ERR);
    fui_text_center(TX0, TX1, LIST_Y + 3, title, C_WHITE);
    if (line1) fui_text_center(TX0, TX1, LIST_Y + 24, line1, C_TEXT);
    if (line2) fui_text_center(TX0, TX1, LIST_Y + 35, line2, C_TEXT);
    for (int frames = 0;; frames++) {
        embers_step();
        fui_present();
        if (frames == 90) fui_cfg_boot_ok();
    }
}
