/*
 * USB HID Wrapper - Header
 * USB keyboard/mouse for the id Tech 1 engines
 * 
 * When USB_HID_ENABLED is not defined, provides empty stub functions
 * so the code compiles but USB HID is disabled.
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef USBHID_WRAPPER_H
#define USBHID_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifdef USB_HID_ENABLED

// Start the TinyUSB host on the native USB port
void usbhid_wrapper_init(void);

// Run the host task (bounded: a port that never enumerates disables the host)
void usbhid_wrapper_poll(void);

int usbhid_wrapper_keyboard_connected(void);
int usbhid_wrapper_mouse_connected(void);

// Next keyboard event as a Doom key; 1 if one was available
int usbhid_wrapper_get_key(int *pressed, unsigned char *key);

// Mouse movement since the last call (dy > 0: away from the user) and the
// buttons (bit 0 left, 1 right, 2 middle); 0 if no mouse is connected
int usbhid_wrapper_get_mouse(int *dx, int *dy, int *wheel, uint8_t *buttons);

// A gamepad is connected; its state (usbhid.h: GP_* buttons, sticks)
#include "usbhid.h"
int usbhid_wrapper_gamepad_connected(void);
void usbhid_wrapper_get_gamepad(usbhid_gamepad_state_t *state);

#else // !USB_HID_ENABLED

static inline void usbhid_wrapper_init(void) {}
static inline void usbhid_wrapper_poll(void) {}
static inline int usbhid_wrapper_keyboard_connected(void) { return 0; }
static inline int usbhid_wrapper_mouse_connected(void) { return 0; }
static inline int usbhid_wrapper_get_key(int *pressed, unsigned char *key) { (void)pressed; (void)key; return 0; }
static inline int usbhid_wrapper_get_mouse(int *dx, int *dy, int *wheel, uint8_t *buttons) {
    (void)dx; (void)dy; (void)wheel; (void)buttons; return 0;
}

typedef struct { uint16_t buttons; int8_t lx, ly, rx, ry; } usbhid_gamepad_state_t;
enum { GP_A = 1, GP_B = 2, GP_X = 4, GP_Y = 8, GP_LB = 16, GP_RB = 32, GP_LT = 64, GP_RT = 128,
       GP_L3 = 256, GP_R3 = 512, GP_BACK = 1024, GP_START = 2048, GP_UP = 4096, GP_DOWN = 8192,
       GP_LEFT = 16384, GP_RIGHT = 32768 };
static inline int usbhid_wrapper_gamepad_connected(void) { return 0; }
static inline void usbhid_wrapper_get_gamepad(usbhid_gamepad_state_t *s) { (void)s; }

#endif // USB_HID_ENABLED

#ifdef __cplusplus
}
#endif

#endif /* USBHID_WRAPPER_H */
