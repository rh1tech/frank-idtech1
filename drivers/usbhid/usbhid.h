/*
 * USB HID Host Driver Header
 * Provides keyboard and mouse input via USB Host
 * 
 * SPDX-License-Identifier: MIT
 */

#ifndef USBHID_H
#define USBHID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// USB HID State
//--------------------------------------------------------------------

// USB keyboard state accessible from wrapper
typedef struct {
    uint8_t keycode[6];     // Currently pressed keys (HID keycodes)
    uint8_t modifier;       // Modifier keys (shift, ctrl, alt, etc.)
    int has_key;            // Non-zero if a key event is pending
} usbhid_keyboard_state_t;

// USB mouse state
typedef struct {
    int16_t dx;             // Accumulated X movement
    int16_t dy;             // Accumulated Y movement
    int8_t wheel;           // Wheel movement
    uint8_t buttons;        // Button state (bit 0=left, 1=right, 2=middle)
    int has_motion;         // Non-zero if motion/button change occurred
} usbhid_mouse_state_t;

// Gamepad state (gamepad.c): buttons, and sticks -127..127 (up/left negative)
enum {
    GP_A = 1u << 0, GP_B = 1u << 1, GP_X = 1u << 2, GP_Y = 1u << 3,
    GP_LB = 1u << 4, GP_RB = 1u << 5, GP_LT = 1u << 6, GP_RT = 1u << 7,
    GP_L3 = 1u << 8, GP_R3 = 1u << 9, GP_BACK = 1u << 10, GP_START = 1u << 11,
    GP_UP = 1u << 12, GP_DOWN = 1u << 13, GP_LEFT = 1u << 14, GP_RIGHT = 1u << 15,
};

typedef struct {
    uint16_t buttons;
    int8_t lx, ly, rx, ry;
} usbhid_gamepad_state_t;

int usbhid_gamepad_connected(void);
void usbhid_get_gamepad_state(usbhid_gamepad_state_t *state);

// hid_app.c -> gamepad.c
#include <stdbool.h>
bool gamepad_hid_mount(uint8_t instance, uint16_t vid, uint16_t pid);
void gamepad_hid_umount(uint8_t instance);
bool gamepad_hid_report(uint8_t instance, uint8_t const *report, uint16_t len);
void gamepad_hid_generic(uint8_t instance, uint8_t const *report, uint16_t len);

//--------------------------------------------------------------------
// API Functions
//--------------------------------------------------------------------

/**
 * Initialize USB Host HID driver
 * Call this during system initialization
 */
void usbhid_init(void);

/**
 * Poll USB Host for events
 * Must be called periodically (e.g., every frame)
 */
void usbhid_task(void);

/**
 * Check if a USB keyboard is connected
 * @return Non-zero if keyboard connected
 */
int usbhid_keyboard_connected(void);

/**
 * Check if a USB mouse is connected
 * @return Non-zero if mouse connected
 */
int usbhid_mouse_connected(void);

/**
 * Get current keyboard state
 * @param state Pointer to state structure to fill
 */
void usbhid_get_keyboard_state(usbhid_keyboard_state_t *state);

/**
 * Get current mouse state and reset deltas
 * @param state Pointer to state structure to fill
 */
void usbhid_get_mouse_state(usbhid_mouse_state_t *state);

/**
 * Get pending key action (for wrapper compatibility)
 * @param keycode HID keycode of the key
 * @param down Non-zero if key pressed, 0 if released
 * @return Non-zero if action available
 */
int usbhid_get_key_action(uint8_t *keycode, int *down);

#ifdef __cplusplus
}
#endif

#endif /* USBHID_H */
