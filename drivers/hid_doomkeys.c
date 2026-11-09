/*
 * HID usage (keyboard page) -> Doom key, shared by the PS/2 and USB keyboard
 * wrappers (the PS/2 driver delivers HID reports too).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hid_doomkeys.h"
#include "doomkeys.h"

static const unsigned char table[256] = {
    [0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd', [0x08] = 'e', [0x09] = 'f',
    [0x0A] = 'g', [0x0B] = 'h', [0x0C] = 'i', [0x0D] = 'j', [0x0E] = 'k', [0x0F] = 'l',
    [0x10] = 'm', [0x11] = 'n', [0x12] = 'o', [0x13] = 'p', [0x14] = 'q', [0x15] = 'r',
    [0x16] = 's', [0x17] = 't', [0x18] = 'u', [0x19] = 'v', [0x1A] = 'w', [0x1B] = 'x',
    [0x1C] = 'y', [0x1D] = 'z',
    [0x1E] = '1', [0x1F] = '2', [0x20] = '3', [0x21] = '4', [0x22] = '5', [0x23] = '6',
    [0x24] = '7', [0x25] = '8', [0x26] = '9', [0x27] = '0',
    [0x28] = KEY_ENTER, [0x29] = KEY_ESCAPE, [0x2A] = KEY_BACKSPACE, [0x2B] = KEY_TAB,
    [0x2C] = KEY_USE, [0x2D] = KEY_MINUS, [0x2E] = KEY_EQUALS, [0x2F] = '[', [0x30] = ']',
    [0x31] = '\\', [0x32] = '\\', [0x33] = ';', [0x34] = '\'', [0x35] = '`', [0x36] = ',',
    [0x37] = '.', [0x38] = '/', [0x39] = KEY_CAPSLOCK,
    [0x3A] = KEY_F1, [0x3B] = KEY_F2, [0x3C] = KEY_F3, [0x3D] = KEY_F4, [0x3E] = KEY_F5,
    [0x3F] = KEY_F6, [0x40] = KEY_F7, [0x41] = KEY_F8, [0x42] = KEY_F9, [0x43] = KEY_F10,
    [0x44] = KEY_F11, [0x45] = KEY_F12,
    [0x46] = KEY_PRTSCR, [0x47] = KEY_SCRLCK, [0x48] = KEY_PAUSE,
    [0x49] = KEY_INS, [0x4A] = KEY_HOME, [0x4B] = KEY_PGUP, [0x4C] = KEY_DEL,
    [0x4D] = KEY_END, [0x4E] = KEY_PGDN, [0x4F] = KEY_RIGHTARROW, [0x50] = KEY_LEFTARROW,
    [0x51] = KEY_DOWNARROW, [0x52] = KEY_UPARROW,
    [0x53] = KEY_NUMLOCK, [0x54] = KEYP_DIVIDE, [0x55] = KEYP_MULTIPLY, [0x56] = KEYP_MINUS,
    [0x57] = KEYP_PLUS, [0x58] = KEYP_ENTER,
    [0x59] = KEYP_1, [0x5A] = KEYP_2, [0x5B] = KEYP_3, [0x5C] = KEYP_4, [0x5D] = KEYP_5,
    [0x5E] = KEYP_6, [0x5F] = KEYP_7, [0x60] = KEYP_8, [0x61] = KEYP_9, [0x62] = KEYP_0,
    [0x63] = KEY_DEL, [0x64] = '\\', [0x67] = KEYP_EQUALS,
    // drivers/usbhid reports the modifiers as these, left and right merged
    [0xE0] = KEY_FIRE, [0xE1] = KEY_RSHIFT, [0xE2] = KEY_RALT,
};

// 0 US, 1 UK, 2 German (QWERTZ), 3 French (AZERTY): the unshifted keys that
// differ from US. The digit row stays digits (weapon keys).
int keyboard_layout = 0;

static unsigned char layout_key(uint8_t usage) {
    switch (keyboard_layout) {
    case 1:  // UK
        if (usage == 0x32) return '#';
        break;
    case 2:  // German
        switch (usage) {
        case 0x1C: return 'z';   // Y position
        case 0x1D: return 'y';   // Z position
        case 0x2D: return '/';   // sharp s
        case 0x2E: return '\''; // acute accent
        case 0x2F: return '[';   // u umlaut
        case 0x30: return '+';
        case 0x32: return '#';
        case 0x33: return ';';   // o umlaut
        case 0x34: return '\''; // a umlaut
        case 0x35: return '^';
        case 0x38: return '-';
        case 0x64: return '<';
        }
        break;
    case 3:  // French
        switch (usage) {
        case 0x04: return 'q';
        case 0x14: return 'a';
        case 0x1A: return 'z';
        case 0x1D: return 'w';
        case 0x10: return ',';
        case 0x33: return 'm';
        case 0x36: return ';';
        case 0x37: return ':';
        case 0x38: return '!';
        case 0x2D: return ')';
        case 0x2F: return '^';
        case 0x30: return '$';
        case 0x34: return 'u';   // u grave
        case 0x35: return '`';   // superscript 2
        case 0x32: return '*';
        case 0x64: return '<';
        }
        break;
    }
    return table[usage];
}

unsigned char hid_to_doomkey(uint8_t usage) {
    return keyboard_layout ? layout_key(usage) : table[usage];
}
