#include "../../src/board_config.h"
#include "ps2kbd_wrapper.h"
#include "ps2kbd_mrmltr.h"
#include "doomkeys.h"
#include "../hid_doomkeys.h"
#include <queue>

struct KeyEvent {
    int pressed;
    unsigned char key;
};

static std::queue<KeyEvent> event_queue;

static unsigned char hid_to_doom(uint8_t code) {
    return hid_to_doomkey(code);
}

static void key_handler(hid_keyboard_report_t *curr, hid_keyboard_report_t *prev) {
    // Check modifiers - Map Ctrl to FIRE, Alt to ALT (for strafe)
    uint8_t changed_mods = curr->modifier ^ prev->modifier;
    if (changed_mods) {
        // Map Ctrl (left or right) to KEY_FIRE for shooting
        if (changed_mods & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL)) {
            int ctrl_pressed = (curr->modifier & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL)) != 0;
            event_queue.push({ctrl_pressed, KEY_FIRE});  // Changed from KEY_RCTRL to KEY_FIRE
        }
        // Map Shift to Shift (for running)
        if (changed_mods & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT)) {
            int shift_pressed = (curr->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT)) != 0;
            event_queue.push({shift_pressed, KEY_RSHIFT});
        }
        // Map Alt to Alt (for strafing)
        if (changed_mods & (KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_RIGHTALT)) {
            int alt_pressed = (curr->modifier & (KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_RIGHTALT)) != 0;
            event_queue.push({alt_pressed, KEY_RALT});
        }
    }

    // Check keys
    for (int i = 0; i < 6; i++) {
        if (curr->keycode[i] != 0) {
            bool found = false;
            for (int j = 0; j < 6; j++) {
                if (prev->keycode[j] == curr->keycode[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                unsigned char k = hid_to_doom(curr->keycode[i]);
                if (k) event_queue.push({1, k});
            }
        }
    }

    for (int i = 0; i < 6; i++) {
        if (prev->keycode[i] != 0) {
            bool found = false;
            for (int j = 0; j < 6; j++) {
                if (curr->keycode[j] == prev->keycode[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                unsigned char k = hid_to_doom(prev->keycode[i]);
                if (k) event_queue.push({0, k});
            }
        }
    }
}

static Ps2Kbd_Mrmltr* kbd = nullptr;

extern "C" void ps2kbd_init(void) {
    // PS2 keyboard driver expects base_gpio as CLK, and base_gpio+1 as DATA
    // For M1: PS2_PIN_CLK=0, PS2_PIN_DATA=1, so base should be PS2_PIN_CLK
    // For M2: PS2_PIN_CLK=2, PS2_PIN_DATA=3, so base should be PS2_PIN_CLK
#ifndef PS2KBD_PIO
#define PS2KBD_PIO pio0
#endif
    kbd = new Ps2Kbd_Mrmltr(PS2KBD_PIO, PS2_PIN_CLK, key_handler);
    kbd->init_gpio();
}

extern "C" void ps2kbd_tick(void) {
    if (kbd) kbd->tick();
}

extern "C" int ps2kbd_get_key(int* pressed, unsigned char* key) {
    if (event_queue.empty()) return 0;
    KeyEvent e = event_queue.front();
    event_queue.pop();
    *pressed = e.pressed;
    *key = e.key;
    return 1;
}
