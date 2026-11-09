// PS/2 Mouse Wrapper for DOOM
// Interfaces ps2mouse driver with DOOM's event system
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps2mouse_wrapper.h"
#include "ps2mouse.h"
#include <stdint.h>

// Shared with the USB mouse: scaling, clamping, Y invert, ev_mouse
// (src/doomgeneric_rp2350.c)
extern void DG_PostMouse(int buttons, int dx, int dy, int wheel);

// Track previous button state to detect changes
static uint8_t prev_buttons = 0;

void ps2mouse_wrapper_init(void) {
    ps2mouse_init();
    prev_buttons = 0;
}

void ps2mouse_wrapper_tick(void) {
    int16_t dx, dy;
    int8_t wheel;
    uint8_t buttons;

    // Get accumulated mouse movement
    int has_motion = ps2mouse_get_state(&dx, &dy, &wheel, &buttons);

    // PS/2 button mapping matches DOOM: bit 0 = left, bit 1 = right, bit 2 = middle.
    // X is positive when moving right, Y positive when moving away from the user.
    buttons &= 0x07;
    if (has_motion || buttons != prev_buttons) {
        DG_PostMouse(buttons, dx, dy, wheel);
        prev_buttons = buttons;
    }
}
