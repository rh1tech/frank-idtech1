/*
 * USB gamepads, decoded into one pad state (usbhid.h).
 *
 * Ported from pico-speccy (drivers/usbhost/hid_app.cpp, joy_hid.cpp):
 * XInput pads through xinput_host.c (Xbox 360 / One / OG, Logitech F710 in
 * X mode), Sony DualShock 4 and DualSense, Logitech F710 in D mode, the
 * 2563:0575, FEED:2320 and 0810:0001 clones, and any other HID gamepad or
 * joystick with the DragonRise-family layout pico-speccy falls back to.
 *
 * SPDX-License-Identifier: MIT
 */

#include "tusb.h"
#include "usbhid.h"
#include "xinput_host.h"
#include <string.h>

#if CFG_TUH_ENABLED

static usbhid_gamepad_state_t pad;
static uint16_t pad_vid[CFG_TUH_HID], pad_pid[CFG_TUH_HID];
static uint32_t hid_pads;      // bit per HID instance decoded as a gamepad
static uint32_t xinput_pads;   // bit per XInput instance

int usbhid_gamepad_connected(void) {
    return (hid_pads | xinput_pads) != 0;
}

void usbhid_get_gamepad_state(usbhid_gamepad_state_t *s) {
    *s = pad;
}

/* 8-bit axis, 0x80 centre -> -127..127 */
static int8_t axis8(uint8_t v) {
    const int a = (int)v - 128;
    return (int8_t)(a < -127 ? -127 : a);
}

static uint16_t hat_bits(uint8_t hat) {
    static const uint16_t dirs[8] = {
        GP_UP, GP_UP | GP_RIGHT, GP_RIGHT, GP_DOWN | GP_RIGHT,
        GP_DOWN, GP_DOWN | GP_LEFT, GP_LEFT, GP_UP | GP_LEFT,
    };
    return hat < 8 ? dirs[hat] : 0;
}

static void set_pad(uint16_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry) {
    pad.buttons = buttons;
    pad.lx = axis8(lx);
    pad.ly = axis8(ly);
    pad.rx = axis8(rx);
    pad.ry = axis8(ry);
}

#define BIT(cond, b) ((cond) ? (b) : 0)

/* ---- vendor layouts (pico-speccy's, reports without the report id) ---- */

// Sony DualShock 4: [0..3] LX LY RX RY, [4] hat + square/cross/circle/triangle,
// [5] L1 R1 L2 R2 share options L3 R3
static void ds4(uint8_t const *r, uint16_t len) {
    if (len < 7) return;
    const uint8_t f = r[4], b = r[5];
    set_pad(hat_bits(f & 0x0F) | BIT(f & 0x20, GP_A) | BIT(f & 0x40, GP_B) | BIT(f & 0x10, GP_X)
                | BIT(f & 0x80, GP_Y) | BIT(b & 0x01, GP_LB) | BIT(b & 0x02, GP_RB)
                | BIT(b & 0x04, GP_LT) | BIT(b & 0x08, GP_RT) | BIT(b & 0x10, GP_BACK)
                | BIT(b & 0x20, GP_START) | BIT(b & 0x40, GP_L3) | BIT(b & 0x80, GP_R3),
            r[0], r[1], r[2], r[3]);
}

// Sony DualSense: [0..3] sticks, [7] hat + face, [8] shoulders/create/options/L3/R3
static void ds5(uint8_t const *r, uint16_t len) {
    if (len < 9) return;
    const uint8_t f = r[7], b = r[8];
    set_pad(hat_bits(f & 0x0F) | BIT(f & 0x20, GP_A) | BIT(f & 0x40, GP_B) | BIT(f & 0x10, GP_X)
                | BIT(f & 0x80, GP_Y) | BIT(b & 0x01, GP_LB) | BIT(b & 0x02, GP_RB)
                | BIT(b & 0x04, GP_LT) | BIT(b & 0x08, GP_RT) | BIT(b & 0x10, GP_BACK)
                | BIT(b & 0x20, GP_START) | BIT(b & 0x40, GP_L3) | BIT(b & 0x80, GP_R3),
            r[0], r[1], r[2], r[3]);
}

// Logitech F710, D mode: [0..3] sticks, [4] hat + X A B Y, [5] LB RB LT RT back start L3 R3
static void f710(uint8_t const *r, uint16_t len) {
    if (len < 7) return;
    const uint8_t f = r[4], b = r[5];
    set_pad(hat_bits(f & 0x0F) | BIT(f & 0x10, GP_X) | BIT(f & 0x20, GP_A) | BIT(f & 0x40, GP_B)
                | BIT(f & 0x80, GP_Y) | BIT(b & 0x01, GP_LB) | BIT(b & 0x02, GP_RB)
                | BIT(b & 0x04, GP_LT) | BIT(b & 0x08, GP_RT) | BIT(b & 0x10, GP_BACK)
                | BIT(b & 0x20, GP_START) | BIT(b & 0x40, GP_L3) | BIT(b & 0x80, GP_R3),
            r[0], r[1], r[2], r[3]);
}

// 2563:0575 (8BitDo / clones): [0] Y B A X L R, [1] select start, [2] hat, [3..6] sticks
static void gp_2563(uint8_t const *r, uint16_t len) {
    if (len < 7) return;
    const uint8_t b0 = r[0], b1 = r[1];
    set_pad(hat_bits(r[2] & 0x0F) | BIT(b0 & 0x01, GP_Y) | BIT(b0 & 0x02, GP_B)
                | BIT(b0 & 0x04, GP_A) | BIT(b0 & 0x08, GP_X) | BIT(b0 & 0x10, GP_LB)
                | BIT(b0 & 0x20, GP_RB) | BIT(b1 & 0x01, GP_BACK) | BIT(b1 & 0x02, GP_START),
            r[3], r[4], r[5], r[6]);
}

// FEED:2320: [0..3] sticks, [4] hat, [5] A B Y X L R, [6] select start
static void gp_feed(uint8_t const *r, uint16_t len) {
    if (len < 7) return;
    const uint8_t b5 = r[5], b6 = r[6];
    set_pad(hat_bits(r[4] & 0x0F) | BIT(b5 & 0x01, GP_A) | BIT(b5 & 0x02, GP_B)
                | BIT(b5 & 0x04, GP_Y) | BIT(b5 & 0x08, GP_X) | BIT(b5 & 0x10, GP_LB)
                | BIT(b5 & 0x20, GP_RB) | BIT(b6 & 0x04, GP_BACK) | BIT(b6 & 0x08, GP_START),
            r[0], r[1], r[2], r[3]);
}

// 0810:0001 "Game Stick Lite": [2] LX, [3] LY, [4] Y B A X (high nibble),
// [5] LB RB LT RT select start
static void gp_0810(uint8_t const *r, uint16_t len) {
    if (len < 7) return;
    const uint8_t b4 = r[4], b5 = r[5];
    set_pad(BIT(b4 & 0x10, GP_Y) | BIT(b4 & 0x20, GP_B) | BIT(b4 & 0x40, GP_A)
                | BIT(b4 & 0x80, GP_X) | BIT(b5 & 0x01, GP_LB) | BIT(b5 & 0x02, GP_RB)
                | BIT(b5 & 0x04, GP_LT) | BIT(b5 & 0x08, GP_RT) | BIT(b5 & 0x10, GP_BACK)
                | BIT(b5 & 0x20, GP_START),
            r[2], r[3], 0x80, 0x80);
}

static bool is_ds4(uint16_t v, uint16_t p) { return v == 0x054C && (p == 0x05C4 || p == 0x09CC); }
static bool is_ds5(uint16_t v, uint16_t p) { return v == 0x054C && p == 0x0CE6; }
static bool is_f710d(uint16_t v, uint16_t p) { return v == 0x046D && p == 0xC219; }
static bool is_2563(uint16_t v, uint16_t p) { return v == 0x2563 && p == 0x0575; }
static bool is_feed(uint16_t v, uint16_t p) { return v == 0xFEED && p == 0x2320; }
static bool is_0810(uint16_t v, uint16_t p) { return v == 0x0810 && p == 0x0001; }

bool gamepad_hid_mount(uint8_t instance, uint16_t vid, uint16_t pid) {
    if (instance >= CFG_TUH_HID) return false;
    pad_vid[instance] = vid;
    pad_pid[instance] = pid;
    const bool known = is_ds4(vid, pid) || is_ds5(vid, pid) || is_f710d(vid, pid)
                    || is_2563(vid, pid) || is_feed(vid, pid) || is_0810(vid, pid);
    if (known) hid_pads |= 1u << instance;
    return is_0810(vid, pid);  // a boot-protocol dongle: wants report protocol
}

void gamepad_hid_umount(uint8_t instance) {
    if (instance >= CFG_TUH_HID) return;
    hid_pads &= ~(1u << instance);
    pad_vid[instance] = pad_pid[instance] = 0;
    if (!usbhid_gamepad_connected()) memset(&pad, 0, sizeof(pad));
}

// The report as it arrived (report id included); true if it was a known pad's
bool gamepad_hid_report(uint8_t instance, uint8_t const *r, uint16_t len) {
    if (instance >= CFG_TUH_HID || len == 0) return false;
    const uint16_t v = pad_vid[instance], p = pad_pid[instance];
    if (is_ds4(v, p)) { if (len >= 10 && r[0] == 0x01) ds4(r + 1, len - 1); return true; }
    if (is_ds5(v, p)) { if (len >= 11 && r[0] == 0x01) ds5(r + 1, len - 1); return true; }
    if (is_f710d(v, p)) { if (len >= 8 && r[0] == 0x01) f710(r + 1, len - 1); return true; }
    if (is_2563(v, p)) { gp_2563(r, len); return true; }
    if (is_feed(v, p)) { if (len >= 8 && r[0] == 0x07) gp_feed(r + 1, len - 1); return true; }
    if (is_0810(v, p)) {
        if (len >= 8 && (r[0] == 0x01 || r[0] == 0x02)) gp_0810(r + 1, len - 1);
        return true;
    }
    return false;
}

// of two axis bytes, the one further from centre
static uint8_t pick_axis(uint8_t a, uint8_t b) {
    const int da = a > 0x80 ? a - 0x80 : 0x7F - a, db = b > 0x80 ? b - 0x80 : 0x7F - b;
    return db > da ? b : a;
}

// Any other gamepad/joystick usage: the DragonRise-family layout,
// [5] X A B Y (high nibble), [6] L R LT RT select start. The stick is in
// [0] [1] on some (pico-speccy) and in [3] [4] on others (murmgenesis):
// whichever pair is off centre.
void gamepad_hid_generic(uint8_t instance, uint8_t const *r, uint16_t len) {
    if (instance < CFG_TUH_HID) hid_pads |= 1u << instance;
    if (len < 7) return;
    const uint8_t b5 = r[5], b6 = r[6];
    set_pad(BIT(b5 & 0x10, GP_X) | BIT(b5 & 0x20, GP_A) | BIT(b5 & 0x40, GP_B)
                | BIT(b5 & 0x80, GP_Y) | BIT(b6 & 0x01, GP_LB) | BIT(b6 & 0x02, GP_RB)
                | BIT(b6 & 0x04, GP_LT) | BIT(b6 & 0x08, GP_RT) | BIT(b6 & 0x10, GP_BACK)
                | BIT(b6 & 0x20, GP_START),
            pick_axis(r[0], r[3]), pick_axis(r[1], r[4]), 0x80, 0x80);
}

/* ---- XInput (xinput_host.c), as pico-speccy's joy_hid.cpp ---- */

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &usbh_xinput_driver;
}

static int8_t axis16(int16_t v, bool flip) {
    int a = v / 256;
    if (flip) a = -a - 1;
    return (int8_t)(a < -127 ? -127 : a > 127 ? 127 : a);
}

void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    (void)len;
    const xinputh_interface_t *xid = (const xinputh_interface_t *)report;
    const xinput_gamepad_t *p = &xid->pad;
    const uint16_t b = p->wButtons;

    // The F710 in XInput mode (046D:C21F) reports garbage in the trigger bytes
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    const bool f710x = vid == 0x046D && pid == 0xC21F;

    if (xid->connected) {
        pad.buttons = BIT(b & XINPUT_GAMEPAD_DPAD_UP, GP_UP) | BIT(b & XINPUT_GAMEPAD_DPAD_DOWN, GP_DOWN)
                    | BIT(b & XINPUT_GAMEPAD_DPAD_LEFT, GP_LEFT) | BIT(b & XINPUT_GAMEPAD_DPAD_RIGHT, GP_RIGHT)
                    | BIT(b & XINPUT_GAMEPAD_A, GP_A) | BIT(b & XINPUT_GAMEPAD_B, GP_B)
                    | BIT(b & XINPUT_GAMEPAD_X, GP_X) | BIT(b & XINPUT_GAMEPAD_Y, GP_Y)
                    | BIT(b & XINPUT_GAMEPAD_LEFT_SHOULDER, GP_LB) | BIT(b & XINPUT_GAMEPAD_RIGHT_SHOULDER, GP_RB)
                    | BIT(!f710x && p->bLeftTrigger > 0x40, GP_LT) | BIT(!f710x && p->bRightTrigger > 0x40, GP_RT)
                    | BIT(b & XINPUT_GAMEPAD_BACK, GP_BACK) | BIT(b & XINPUT_GAMEPAD_START, GP_START)
                    | BIT(b & XINPUT_GAMEPAD_LEFT_THUMB, GP_L3) | BIT(b & XINPUT_GAMEPAD_RIGHT_THUMB, GP_R3);
        pad.lx = axis16(p->sThumbLX, false);
        pad.ly = axis16(p->sThumbLY, true);   // XInput: up is positive
        pad.rx = axis16(p->sThumbRX, false);
        pad.ry = axis16(p->sThumbRY, true);
    }
    tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, const xinputh_interface_t *xinput_itf) {
    if (instance < 32) xinput_pads |= 1u << instance;
    // An Xbox 360 wireless receiver: wait for a pad to connect before the LEDs
    if (xinput_itf->type == XBOX360_WIRELESS && xinput_itf->connected == false) {
        tuh_xinput_receive_report(dev_addr, instance);
        return;
    }
    tuh_xinput_set_led(dev_addr, instance, 0, true);
    tuh_xinput_set_led(dev_addr, instance, 1, true);
    tuh_xinput_set_rumble(dev_addr, instance, 0, 0, true);
    tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr;
    if (instance < 32) xinput_pads &= ~(1u << instance);
    if (!usbhid_gamepad_connected()) memset(&pad, 0, sizeof(pad));
}

#endif // CFG_TUH_ENABLED
