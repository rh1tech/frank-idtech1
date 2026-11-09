/*
 * The firmware's video API (HDMI.h) on frank-quake2's PIO HDMI driver
 * (drivers/hdmi_pio). The driver pulls each 320-pixel row through a
 * callback and runs its line IRQ on core 1, which has nothing else to do:
 * the game keeps core 0 to itself.
 *
 * A flip and its palette are one request: core 0 encodes the palette into
 * the driver's staging table, then asks for the buffer; at the next vertical
 * blank the vsync callback (core 1) switches the buffer and shows the
 * palette in the same blank, so a frame never meets the other's colours.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "HDMI.h"
#include "hdmi_pio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include <string.h>

#define W 320
#define H 240

static uint8_t *volatile front;            // the buffer on screen
static uint8_t *req_buffer;                // the request: a buffer,
static bool req_palette;                   // its palette (staged in the driver)
static volatile bool req_go;               // and the go, read by core 1

static uint8_t rgb[256 * 3];               // the palette being staged
static volatile int shift_x, shift_y;

static uint8_t black_row[W];
volatile uint32_t vh_late_rows;             // rows the prefetcher missed (SWD)

/* The rows come from PSRAM, which core 0 keeps busy; read there by the line
 * IRQ, a slow one missed the IRQ's one-line deadline and stopped the video.
 * Core 1 has nothing else to do: it copies the rows a few lines ahead of the
 * beam into SRAM (moved sideways on the way, for the picture shift), and the
 * IRQ converts from there. A row not ready in time is read from PSRAM. */
#define RING 8                               // rows; the IRQ wants one per 63.5 us
static uint8_t ring[RING][W] __attribute__((aligned(4)));
static volatile int32_t ring_row[RING];      // the source row a slot holds, or -1
static uint8_t *volatile ring_fb[RING];      // ... of this buffer

static const uint8_t *__not_in_flash_func(row_cb)(uint32_t row) {
    uint8_t *fb = front;
    const int y = (int)row - shift_y;
    if (!fb || y < 0 || y >= H) return black_row;
    const int s = row % RING;
    if (ring_row[s] == (int32_t)row && ring_fb[s] == fb) return ring[s];
    vh_late_rows++;                          // not prefetched: never PSRAM here
    return black_row;
}

static void __not_in_flash_func(fill_row)(int s, uint32_t row, uint8_t *fb) {
    const int y = (int)row - shift_y, sx = shift_x;
    uint32_t *d = (uint32_t *)ring[s];
    ring_row[s] = -1;
    __dmb();
    if (y < 0 || y >= H) {
        for (int i = 0; i < W / 4; i++) d[i] = 0;
    } else if (!sx) {
        const uint32_t *src = (const uint32_t *)(fb + y * W);
        for (int i = 0; i < W / 4; i++) d[i] = src[i];
    } else {
        // the border is index 0, black in every game's palette and the UI's
        const uint8_t *src = fb + y * W;
        uint8_t *b = ring[s];
        for (int x = 0; x < W; x++) {
            const int sx0 = x - sx;
            b[x] = sx0 >= 0 && sx0 < W ? src[sx0] : 0;
        }
    }
    ring_fb[s] = fb;
    __dmb();
    ring_row[s] = (int32_t)row;
}

static void __not_in_flash_func(vsync_cb)(void) {
    if (!req_go) return;
    if (req_buffer) front = req_buffer;
    if (req_palette) hdmi_pio_show_palette();  // applied right after this call
    req_buffer = NULL;
    req_palette = false;
    __dmb();
    req_go = false;
}

static uint32_t core1_stack[1024];  // 4 KB: SCRATCH_X/Y belong to the driver

static void __not_in_flash_func(core1_main)(void) {
    for (int s = 0; s < RING; s++) ring_row[s] = -1;
    psxv_start();  // binds the line IRQ to this core
    for (;;) {
        // the source rows of the next RING output line pairs (row = line / 2)
        const uint32_t line = psxv_line;
        uint32_t next = line < 480 ? line / 2 + 1 : 0;
        // The flip happens at line 481 (the vsync callback): before it, the
        // next frame shows the requested buffer if there is one; from it on,
        // whatever is in front -- a request made later waits a frame.
        uint8_t *fb = (line < 480 || line > 481) ? front
                    : (req_go && req_buffer ? req_buffer : front);
        for (int k = 0; k < RING - 1 && next + k < H; k++) {
            const uint32_t row = next + k;
            const int s = row % RING;
            if ((ring_row[s] != (int32_t)row || ring_fb[s] != fb) && fb) fill_row(s, row, fb);
        }
    }
}

void graphics_init(g_out g_out) {
    (void)g_out;
    psxv_init();
    hdmi_pio_set_row_callback(row_cb);
    psxv_set_vsync_callback(vsync_cb);
    multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof(core1_stack));
}

void graphics_set_res(int w, int h) {
    (void)w;
    (void)h;
}

void graphics_set_buffer(uint8_t *buffer) {
    req_go = false;
    req_buffer = NULL;
    req_palette = false;
    front = buffer;
}

void graphics_set_shift(int x, int y) {
    shift_x = x < -W + 1 ? -W + 1 : x > W - 1 ? W - 1 : x;
    shift_y = y;
}

void graphics_stage_palette(uint8_t i, uint32_t color888) {
    rgb[i * 3 + 0] = (uint8_t)(color888 >> 16);
    rgb[i * 3 + 1] = (uint8_t)(color888 >> 8);
    rgb[i * 3 + 2] = (uint8_t)color888;
}

void graphics_commit_palette(void) {
    hdmi_pio_load_palette(rgb, 3);  // waits for a palette still to be shown
    req_palette = true;
}

void graphics_request_buffer(uint8_t *buffer) {
    req_buffer = buffer;
    __dmb();
    req_go = true;
}

bool graphics_buffer_pending(void) {
    return req_go;
}

uint32_t graphics_frame_count(void) {
    return psxv_frame_count;
}

bool graphics_wait_vsync(void) {
    const uint32_t f = psxv_frame_count;
    const absolute_time_t until = make_timeout_time_ms(50);
    while (psxv_frame_count == f)
        if (time_reached(until)) return false;
    return true;
}
