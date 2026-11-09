/*
 * Board settings that live outside the games' config files, because they
 * apply before a game is chosen: CPU and PSRAM clocks, the picture shift,
 * the keyboard layout and the WAD started at boot. Plain key=value lines in
 * frank-idtech1.ini at the root of the SD card.
 *
 * A clock that does not work must not lock the board out: before booting
 * with clocks other than the build's, the file is marked "clock_try=1" and
 * the watchdog runs; the welcome screen clears both once it has been up for
 * a while. A boot that crashes or hangs before then comes back with the
 * mark still set, and the clocks go back to the build's.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fui.h"
#include "ff.h"
#include "HDMI.h"
#include "psram_init.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INI_PATH "frank-idtech1.ini"

fui_cfg_t fui_cfg;
bool fui_clocks_reverted;

static bool clock_try;
static bool guard_armed;

extern void set_flash_timings(int cpu_mhz);  // src/main.c
extern int keyboard_layout;                  // drivers/hid_doomkeys.c

static void load(void) {
    memset(&fui_cfg, 0, sizeof(fui_cfg));
    clock_try = false;
    FIL f;
    if (f_open(&f, INI_PATH, FA_READ) != FR_OK) return;
    char line[64];
    while (f_gets(line, sizeof(line), &f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
        const int n = atoi(v);
        if (!strcmp(line, "cpu_mhz")) fui_cfg.cpu_mhz = n;
        else if (!strcmp(line, "psram_mhz")) fui_cfg.psram_mhz = n;
        else if (!strcmp(line, "shift_x")) fui_cfg.shift_x = n;
        else if (!strcmp(line, "shift_y")) fui_cfg.shift_y = n;
        else if (!strcmp(line, "keyboard")) fui_cfg.keyboard = n;
        else if (!strcmp(line, "autostart")) snprintf(fui_cfg.autostart, sizeof(fui_cfg.autostart), "%s", v);
        else if (!strcmp(line, "audio")) fui_cfg.audio = !strcmp(v, "hdmi") ? FUI_AUDIO_HDMI : FUI_AUDIO_I2S;
        else if (!strcmp(line, "clock_try")) clock_try = n != 0;
    }
    f_close(&f);
}

static bool write(bool try_mark) {
    FIL f;
    if (f_open(&f, INI_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    f_printf(&f, "cpu_mhz=%d\npsram_mhz=%d\nshift_x=%d\nshift_y=%d\nkeyboard=%d\nautostart=%s\naudio=%s\nclock_try=%d\n",
             fui_cfg.cpu_mhz, fui_cfg.psram_mhz, fui_cfg.shift_x, fui_cfg.shift_y, fui_cfg.keyboard,
             fui_cfg.autostart, fui_cfg.audio == FUI_AUDIO_HDMI ? "hdmi" : "i2s", try_mark ? 1 : 0);
    return f_close(&f) == FR_OK;
}

bool fui_cfg_save(void) {
    return write(false);
}

static void set_cpu(int mhz) {
    const int now = (int)(clock_get_hz(clk_sys) / 1000000);
    const enum vreg_voltage v = mhz >= 504 ? VREG_VOLTAGE_1_65 : mhz >= 378 ? VREG_VOLTAGE_1_60
                                                                            : VREG_VOLTAGE_1_50;
    if (mhz > now) {  // voltage and flash timing first, then the clock
        vreg_disable_voltage_limit();
        vreg_set_voltage(v);
        sleep_ms(10);
        set_flash_timings(mhz);
        set_sys_clock_khz((uint32_t)mhz * 1000, false);
    } else {          // the clock first, then flash timing and voltage
        set_sys_clock_khz((uint32_t)mhz * 1000, false);
        set_flash_timings(mhz);
        vreg_set_voltage(v);
    }
}

bool fui_cfg_boot(void) {
    load();
    keyboard_layout = fui_cfg.keyboard;
    // HDMI wants a multiple of 252 MHz: an older 378 becomes 504
    if (fui_cfg.cpu_mhz && fui_cfg.cpu_mhz != 252) fui_cfg.cpu_mhz = 504;

    const bool custom = (fui_cfg.cpu_mhz && fui_cfg.cpu_mhz != CPU_CLOCK_MHZ)
                     || (fui_cfg.psram_mhz && fui_cfg.psram_mhz != PSRAM_MAX_FREQ_MHZ);
    if (custom && clock_try) {
        /* the last boot with these clocks did not reach the welcome screen */
        fui_cfg.cpu_mhz = fui_cfg.psram_mhz = 0;
        fui_clocks_reverted = true;
        write(false);
        return false;
    }
    if (!custom) {
        if (clock_try) write(false);
        return false;
    }

    write(true);
    watchdog_enable(8000, true);
    guard_armed = true;
    if (fui_cfg.psram_mhz) psram_max_freq_mhz = fui_cfg.psram_mhz;
    if (fui_cfg.cpu_mhz && fui_cfg.cpu_mhz != (int)(clock_get_hz(clk_sys) / 1000000)) {
        set_cpu(fui_cfg.cpu_mhz);
        return true;  // the SD card's SPI clock moved: mount it again
    }
    return false;
}

void fui_cfg_boot_ok(void) {
    if (!guard_armed) return;
    guard_armed = false;
    watchdog_disable();
    write(false);
}

void fui_cfg_feed(void) {
    if (guard_armed) watchdog_update();
}

void fui_cfg_apply_shift(void) {
    int x = fui_cfg.shift_x, y = fui_cfg.shift_y;
    x = x < -32 ? -32 : x > 32 ? 32 : x;
    y = y < -32 ? -32 : y > 32 ? 32 : y;
    graphics_set_shift(x, y);
}
