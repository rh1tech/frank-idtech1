/*
 * Two frame buffers in PSRAM: one on screen, one drawn into. Flips and
 * palette loads happen at vertical blank (drivers/HDMI.c), so nothing is
 * ever drawn where the beam is reading it.
 *
 * The firmware UI follows frank-amiga's vga_ui_present: draw into the
 * hidden buffer, present it, then copy what changed back into the new
 * hidden buffer so the next drawing starts from the picture on screen.
 * Engine frames are whole: they flip without the copy.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fui.h"
#include "doomgeneric.h"
#include "HDMI.h"
#include "psram_allocator.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include <string.h>

#define FB_BYTES (FUI_W * FUI_H)
#define MAX_DIRTY 8

static uint8_t *fb[2];
static int front_idx;
static bool pal_staged;

typedef struct { int16_t x0, y0, x1, y1; } rect_t;
static rect_t dirty[MAX_DIRTY];
static int ndirty;

void fui_video_init(void) {
    for (int i = 0; i < 2; i++) {
        fb[i] = (uint8_t *)psram_malloc(FB_BYTES);
        if (!fb[i]) panic("fui: OOM for the frame buffers");
        memset(fb[i], 0, FB_BYTES);
    }
    front_idx = 0;
    graphics_set_buffer(fb[0]);
    DG_ScreenBuffer = fb[1];
}

uint8_t *fui_back(void) { return fb[front_idx ^ 1]; }
uint8_t *fui_front(void) { return fb[front_idx]; }

void fui_dirty(int x, int y, int w, int h) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > FUI_W) x1 = FUI_W;
    if (y1 > FUI_H) y1 = FUI_H;
    if (x >= x1 || y >= y1) return;
    // grow a rectangle this one touches; else add one; when full, merge all
    for (int i = 0; i < ndirty; i++) {
        rect_t *r = &dirty[i];
        if (x <= r->x1 && x1 >= r->x0 && y <= r->y1 && y1 >= r->y0) {
            if (x < r->x0) r->x0 = (int16_t)x;
            if (y < r->y0) r->y0 = (int16_t)y;
            if (x1 > r->x1) r->x1 = (int16_t)x1;
            if (y1 > r->y1) r->y1 = (int16_t)y1;
            return;
        }
    }
    if (ndirty == MAX_DIRTY) {
        for (int i = 1; i < ndirty; i++) {
            if (dirty[i].x0 < dirty[0].x0) dirty[0].x0 = dirty[i].x0;
            if (dirty[i].y0 < dirty[0].y0) dirty[0].y0 = dirty[i].y0;
            if (dirty[i].x1 > dirty[0].x1) dirty[0].x1 = dirty[i].x1;
            if (dirty[i].y1 > dirty[0].y1) dirty[0].y1 = dirty[i].y1;
        }
        ndirty = 1;
        fui_dirty(x, y, x1 - x, y1 - y);
        return;
    }
    dirty[ndirty++] = (rect_t){(int16_t)x, (int16_t)y, (int16_t)x1, (int16_t)y1};
}

void fui_dirty_all(void) {
    ndirty = 1;
    dirty[0] = (rect_t){0, 0, FUI_W, FUI_H};
}

void fui_pal(uint8_t i, uint32_t rgb) {
    graphics_stage_palette(i, rgb);
    pal_staged = true;
}

// show fb[front_idx ^ 1] (and the staged palette) from the next frame
static void flip(void) {
    uint8_t *next = fb[front_idx ^ 1];
    const uint32_t irq = save_and_disable_interrupts();  // both in the same blank
    if (pal_staged) graphics_commit_palette();
    graphics_request_buffer(next);
    restore_interrupts(irq);
    pal_staged = false;
    front_idx ^= 1;
    DG_ScreenBuffer = fb[front_idx ^ 1];
}

void fui_wait_back(void) {
    const absolute_time_t until = make_timeout_time_ms(50);  // a frame is 16.7 ms
    while (graphics_buffer_pending()) {
        if (time_reached(until)) {  // no scanout: take the buffer anyway
            graphics_set_buffer(fb[front_idx]);
            break;
        }
        tight_loop_contents();
    }
}

void fui_present(void) {
    if (!ndirty && !pal_staged) return;
    fui_wait_back();
    flip();
    fui_wait_back();
    const uint8_t *src = fb[front_idx];
    uint8_t *dst = fb[front_idx ^ 1];
    for (int i = 0; i < ndirty; i++) {
        const rect_t *r = &dirty[i];
        const int w = r->x1 - r->x0;
        for (int y = r->y0; y < r->y1; y++)
            memcpy(dst + y * FUI_W + r->x0, src + y * FUI_W + r->x0, (size_t)w);
    }
    ndirty = 0;
}

void fui_sync_back(void) {
    fui_wait_back();
    memcpy(fb[front_idx ^ 1], fb[front_idx], FB_BYTES);
    ndirty = 0;
}

void fui_game_frame(bool palette) {
    if (palette) pal_staged = true;  // the caller staged all 256 entries
    flip();
    ndirty = 0;
}
