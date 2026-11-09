/*
 * Keys for the firmware UI, from the PS/2 and USB keyboards. Neither driver
 * repeats a held key, so the navigation keys repeat here (frank-amiga's
 * timing: 500 ms, then 30 per second).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fui.h"
#include "doomkeys.h"
#include "pico/stdlib.h"

// src/doomgeneric_rp2350.c: polls the keyboards, drops mouse input
extern int DG_UIKey(int *pressed, unsigned char *key);

#define REPEAT_DELAY_MS 500
#define REPEAT_RATE_MS 33

static int held = -1;
static uint32_t next_repeat;

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static bool repeats(int k) {
    return k == KEY_UPARROW || k == KEY_DOWNARROW || k == KEY_LEFTARROW || k == KEY_RIGHTARROW
        || k == KEY_PGUP || k == KEY_PGDN;
}

int fui_key(int timeout_ms) {
    const uint32_t start = now_ms();
    for (;;) {
        int pressed;
        unsigned char k;
        while (DG_UIKey(&pressed, &k)) {
            if (pressed) {
                if (k == held) continue;
                held = repeats(k) ? k : -1;
                next_repeat = now_ms() + REPEAT_DELAY_MS;
                return k;
            }
            if (k == held) held = -1;
        }
        const uint32_t t = now_ms();
        if (held >= 0 && (int32_t)(t - next_repeat) >= 0) {
            next_repeat = t + REPEAT_RATE_MS;
            return held;
        }
        if (timeout_ms >= 0 && (int)(t - start) >= timeout_ms) return -1;
        busy_wait_us_32(200);
    }
}

void fui_key_flush(void) {
    int pressed;
    unsigned char k;
    while (DG_UIKey(&pressed, &k)) {}
    held = -1;
}
