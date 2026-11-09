/*
 * USB HID Wrapper for the id Tech 1 engines
 * Keyboard and mouse on the native USB port (TinyUSB host, hid_app.c)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "usbhid.h"
#include "usbhid_wrapper.h"
#include "hid_doomkeys.h"
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include <stdbool.h>
#include <stdint.h>

static int usb_hid_initialized = 0;

void usbhid_wrapper_init(void) {
#ifdef USB_HID_ENABLED
    usbhid_init();
    usb_hid_initialized = 1;
#endif
}

//--------------------------------------------------------------------
// Polling
//--------------------------------------------------------------------

#ifdef USB_HID_ENABLED
/* As in frank-quake2: the host task runs from the main loop, not from an
 * interrupt. TinyUSB enumerates with blocking delays, and something on the
 * port that never enumerates (another host, e.g. a PC on the USB-C port)
 * can keep it looping. A one-shot alarm bounds each call: if one blocks for
 * over a second the USB interrupt is switched off, the call drains and
 * returns, and the host stays off until the next boot. */
static volatile bool usb_dead;
static alarm_id_t usb_guard;

static int64_t usb_guard_cb(alarm_id_t id, void *user) {
    (void)id;
    (void)user;
    usb_dead = true;
    irq_set_enabled(USBCTRL_IRQ, false);
    return 0;
}
#endif

void usbhid_wrapper_poll(void) {
#ifdef USB_HID_ENABLED
    if (!usb_hid_initialized || usb_dead) return;
    usb_guard = add_alarm_in_ms(1000, usb_guard_cb, NULL, true);
    usbhid_task();
    if (usb_guard > 0) cancel_alarm(usb_guard);
#endif
}

//--------------------------------------------------------------------
// Connection status
//--------------------------------------------------------------------

int usbhid_wrapper_keyboard_connected(void) {
#ifdef USB_HID_ENABLED
    if (!usb_hid_initialized) return 0;
    return usbhid_keyboard_connected();
#else
    return 0;
#endif
}

int usbhid_wrapper_mouse_connected(void) {
#ifdef USB_HID_ENABLED
    if (!usb_hid_initialized) return 0;
    return usbhid_mouse_connected();
#else
    return 0;
#endif
}

//--------------------------------------------------------------------
// Events
//--------------------------------------------------------------------

int usbhid_wrapper_get_key(int *pressed, unsigned char *key) {
#ifdef USB_HID_ENABLED
    if (!usb_hid_initialized) return 0;

    uint8_t hid_keycode;
    int down;
    while (usbhid_get_key_action(&hid_keycode, &down)) {
        unsigned char doom_key = hid_to_doomkey(hid_keycode);
        if (doom_key != 0) {
            *pressed = down;
            *key = doom_key;
            return 1;
        }
    }
#else
    (void)pressed;
    (void)key;
#endif
    return 0;
}

int usbhid_wrapper_get_mouse(int *dx, int *dy, int *wheel, uint8_t *buttons) {
#ifdef USB_HID_ENABLED
    if (!usb_hid_initialized || !usbhid_mouse_connected()) return 0;

    usbhid_mouse_state_t m;
    usbhid_get_mouse_state(&m);
    *dx = m.dx;
    *dy = m.dy;  // already positive = away from the user (hid_app.c)
    *wheel = m.wheel;
    *buttons = m.buttons & 0x07;
    return 1;
#else
    (void)dx;
    (void)dy;
    (void)wheel;
    (void)buttons;
    return 0;
#endif
}

//--------------------------------------------------------------------
// Gamepads (gamepad.c)
//--------------------------------------------------------------------

int usbhid_wrapper_gamepad_connected(void) {
#ifdef USB_HID_ENABLED
    return usb_hid_initialized && usbhid_gamepad_connected();
#else
    return 0;
#endif
}

void usbhid_wrapper_get_gamepad(usbhid_gamepad_state_t *state) {
#ifdef USB_HID_ENABLED
    usbhid_get_gamepad_state(state);
#else
    (void)state;
#endif
}
