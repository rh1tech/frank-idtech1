/*
 * HID usage (keyboard page) -> Doom key, shared by the PS/2 and USB keyboard
 * wrappers. drivers/usbhid reports the modifiers as 0xE0 (Ctrl), 0xE1 (Shift)
 * and 0xE2 (Alt).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HID_DOOMKEYS_H
#define HID_DOOMKEYS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 0: no Doom key for this usage
unsigned char hid_to_doomkey(uint8_t usage);

#ifdef __cplusplus
}
#endif

#endif
