#include "doomgeneric.h"
#include "doomtype.h"
#include "board_config.h"
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "HDMI.h"
#include "psram_init.h"
#include "psram_allocator.h"
#include "sdcard.h"
#include "ff.h"
#include "ps2kbd_wrapper.h"
#include "ps2mouse_wrapper.h"
#include "ps2mouse.h"
#include "usbhid_wrapper.h"
#ifdef USB_HID_ENABLED
#include "usbhid.h"
#endif
#ifdef NESPAD_GPIO_CLK
#include "nespad.h"
#endif
#include "doomkeys.h"
#include "d_event.h"
#include "m_argv.h"
#include "fui.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "pico/bootrom.h"
#include "hardware/structs/psm.h"

/* Every software reboot goes through here (as frank-ecat's platform_reboot):
 * core 1, which runs the HDMI line IRQ and reads PSRAM, is forced off and
 * the PSRAM leaves QPI mode first, or the boot ROM cannot read the flash
 * after the warm reset and the chip hangs until a power cycle. */
void __attribute__((noreturn)) platform_reboot(bool bootsel) {
    if (get_core_num() == 0) {
        hw_set_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);
        while (!(psm_hw->frce_off & PSM_FRCE_OFF_PROC1_BITS)) tight_loop_contents();
        busy_wait_us(10);
        /* released again: it restarts into the boot ROM, which waits for a
         * launch; left forced off, the bit survives the watchdog reboot and
         * the chip does not come back */
        hw_clear_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);
        busy_wait_us(10);
    }
    save_and_disable_interrupts();
    psram_exit_qpi();
    if (bootsel) reset_usb_boot(0, 0);
    watchdog_reboot(0, 0, 10);
    while (1) tight_loop_contents();
}

// Game engine type for each IWAD
typedef enum {
    ENGINE_DOOM = 0,
    ENGINE_HERETIC = 1,
    ENGINE_HEXEN = 2,
    ENGINE_STRIFE = 3,
} game_engine_t;

typedef struct {
    const char *filename;
    const char *label;
    game_engine_t engine;
} iwad_entry_t;

// External variables from i_video.c (when CMAP256 is defined)
extern boolean palette_changed;

// Match struct color from i_video.h (Little Endian: b, g, r, a)
extern struct {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
} colors[256];

// External stdio init for FatFS
extern void stdio_fatfs_init(void);

// Global FatFs object
FATFS fs;

// The engine is running (past the WAD selector): F12 opens the settings
static bool in_game;
static bool settings_requested;

// The WAD being played (for "start at boot")
const char *dg_current_wad = "";

void install_fault_handler(void);

static FRESULT mount_sd(void) {
    FRESULT fr = FR_NOT_READY;
    for (int attempt = 0; attempt < 3 && fr != FR_OK; attempt++) fr = f_mount(&fs, "", 1);
    return fr;
}

void DG_Init() {
    install_fault_handler();

    // The SD card first: frank-idtech1.ini may change the CPU and PSRAM
    // clocks, which has to happen before PSRAM and HDMI are set up. FatFs
    // takes its buffers from the SRAM heap until PSRAM works.
    psram_set_sram_mode(1);
    FRESULT fr = mount_sd();
    if (fr == FR_OK && fui_cfg_boot()) {
        f_mount(NULL, "", 0);
        fr = mount_sd();
    }

    // Initialize PSRAM (pin auto-detected based on chip package)
    uint psram_pin = get_psram_pin();
    psram_init(psram_pin);
    psram_set_sram_mode(0); // Use PSRAM

    // Two screen buffers in PSRAM, flipped at vblank (src/fui_video.c);
    // DG_ScreenBuffer is always the hidden one
    fui_video_init();

    // Initialize HDMI
    graphics_init(g_out_HDMI);
    graphics_set_res(320, 240);
    graphics_set_buffer(fui_front());
    fui_cfg_apply_shift();

    if (fr != FR_OK) {
        fui_shell_error("NO SD CARD", "Failed to mount the SD card.",
                        "Insert a FAT32 card with the WADs.", "Restart once the card is in");
    }

    // Set current directory to root (required for relative paths)
    f_chdir("/");

    // Initialize stdio wrapper for FatFS
    stdio_fatfs_init();

#if (defined(FRANK_UART_CONSOLE) || defined(FRANK_NO_PS2_MOUSE)) && (defined(BOARD_M2) || defined(BOARD_Z0))
    ps2kbd_init();             // M2: the mouse's GPIO0/1 belong to the bench
#elif defined(FRANK_UART_CONSOLE)
    ps2mouse_wrapper_init();   // M1: the console has the keyboard's GPIO0/1
#else
    // Initialize PS/2 Keyboard
    ps2kbd_init();

    // Initialize PS/2 Mouse
    ps2mouse_wrapper_init();
#endif

    // USB keyboard, mouse and gamepads on the native USB port
    usbhid_wrapper_init();

#ifdef NESPAD_GPIO_CLK
    // NES/SNES pad on the joystick port
    nespad_begin(clock_get_hz(clk_sys) / 1000, NESPAD_GPIO_CLK, NESPAD_GPIO_DATA, NESPAD_GPIO_LATCH);
#endif
}

void DG_StartScreen(void) {
    // All IWADs for the selector
    static const iwad_entry_t game_iwads[] = {
        {"doom2.wad",    "DOOM II",                ENGINE_DOOM},
        {"doom.wad",     "The Ultimate DOOM",      ENGINE_DOOM},
        {"doom1.wad",    "DOOM Shareware",         ENGINE_DOOM},
        {"plutonia.wad", "Final DOOM: Plutonia",   ENGINE_DOOM},
        {"tnt.wad",      "Final DOOM: TNT",        ENGINE_DOOM},
        {"freedoom2.wad","Freedoom Phase 2",       ENGINE_DOOM},
        {"freedoom1.wad","Freedoom Phase 1",       ENGINE_DOOM},
        {"freedm.wad",   "FreeDM",                 ENGINE_DOOM},
        {"chex.wad",     "Chex Quest",             ENGINE_DOOM},
        {"hacx.wad",     "HACX",                   ENGINE_DOOM},
        {"heretic.wad",  "Heretic",                ENGINE_HERETIC},
        {"heretic1.wad", "Heretic Shareware",      ENGINE_HERETIC},
        {"hexen.wad",    "Hexen",                  ENGINE_HEXEN},
        {"hexdd.wad",    "Hexen: Deathkings",      ENGINE_HEXEN},
        {"strife1.wad",  "Strife",                 ENGINE_STRIFE},
        {"strife0.wad",  "Strife Demo",            ENGINE_STRIFE},
    };
    enum { N_IWADS = sizeof(game_iwads) / sizeof(game_iwads[0]) };

    // Build list of available IWADs.
    // The game engine searches in "idtech1" subdir (see d_iwad.c BuildIWADDirList),
    // so check both root and /idtech1/ on the SD card.
    const iwad_entry_t *available[N_IWADS];
    fui_wad_t list[N_IWADS];
    int available_count = 0, autostart = -1;
    for (int i = 0; i < N_IWADS; ++i) {
        FILINFO info;
        char subpath[64];
        snprintf(subpath, sizeof(subpath), "idtech1/%s", game_iwads[i].filename);
        if (f_stat(game_iwads[i].filename, &info) == FR_OK || f_stat(subpath, &info) == FR_OK) {
            if (!strcmp(fui_cfg.autostart, game_iwads[i].filename)) autostart = available_count;
            available[available_count] = &game_iwads[i];
            list[available_count] = (fui_wad_t){game_iwads[i].label, game_iwads[i].filename, NULL};
            available_count++;
        }
    }

    if (available_count == 0) {
        fui_shell_error("NO WAD FOUND", "Put an IWAD (doom2.wad, heretic.wad,",
                        "hexen.wad, strife1.wad...) in / or /idtech1", "Restart once the WAD is there");
    }

    const int selected = fui_shell_select(list, available_count, autostart,
                                          fui_clocks_reverted ? "Clocks reset: the last start failed" : NULL);

    // Set the active game engine based on the selected WAD
    extern int active_game;
    active_game = (int)available[selected]->engine;

    static char *new_argv[4];
    if (available[selected]->engine == ENGINE_DOOM) {
        new_argv[0] = (char *)"doom";
    } else if (available[selected]->engine == ENGINE_HEXEN) {
        new_argv[0] = (char *)"hexen";
    } else if (available[selected]->engine == ENGINE_STRIFE) {
        new_argv[0] = (char *)"strife";
    } else {
        new_argv[0] = (char *)"heretic";
    }
    new_argv[1] = (char *)"-iwad";
    new_argv[2] = (char *)available[selected]->filename;
    new_argv[3] = NULL;
    myargc = 3;
    myargv = new_argv;
    dg_current_wad = available[selected]->filename;
    in_game = true;
}

//--------------------------------------------------------------------
// Frames
//--------------------------------------------------------------------

int show_fps = 0;  // "show_fps" in the config: frame rate in the top right

static uint8_t fps_ink, fps_shadow;  // the palette's brightest and darkest
static bool fps_inks_stale = true;

static void find_fps_inks(void) {
    int hi = -1, lo = 1 << 30;
    for (int i = 0; i < 240; i++) {
        const int l = colors[i].r * 3 + colors[i].g * 6 + colors[i].b;
        if (l > hi) { hi = l; fps_ink = (uint8_t)i; }
        if (l < lo) { lo = l; fps_shadow = (uint8_t)i; }
    }
    fps_inks_stale = false;
}

// I_FinishUpdate, after the frame is in DG_ScreenBuffer
void DG_Overlay(void) {
    static uint32_t t0, frames;
    static char text[12];
    frames++;
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - t0 >= 1000) {
        snprintf(text, sizeof(text), "%lu FPS", (unsigned long)(frames * 1000 / (now - t0)));
        t0 = now;
        frames = 0;
    }
    if (!show_fps || !text[0]) return;
    if (fps_inks_stale) find_fps_inks();
    const int x = FUI_W - 3 - fui_text_w(text);
    fui_text(x + 1, 3, text, fps_shadow);
    fui_text(x, 2, text, fps_ink);
}

// The engine's frame is in DG_ScreenBuffer: show it at the next vblank,
// with the palette if it changed (both in the same blank)
void DG_DrawFrame() {
    const bool pal = palette_changed;
    if (pal) {
        for (int i = 0; i < 256; i++) {
            uint32_t color = (colors[i].r << 16) | (colors[i].g << 8) | colors[i].b;
            graphics_stage_palette((uint8_t)i, color);
        }
        palette_changed = false;
        fps_inks_stale = true;
    }
    fui_game_frame(pal);
}

// I_FinishUpdate: wait for the last flip before drawing into the hidden buffer
void DG_WaitBackBuffer(void) {
    fui_wait_back();
}

// Game time stops while the settings window is open
static uint32_t paused_ms;

void DG_SleepMs(uint32_t ms) {
    sleep_ms(ms);
}

uint32_t DG_GetTicksMs() {
    return to_ms_since_boot(get_absolute_time()) - paused_ms;
}

//--------------------------------------------------------------------
// Mouse
//--------------------------------------------------------------------

// Shared by the PS/2 and USB mice: clamp, scale, accelerate, invert, post
// ev_mouse. Y > 0: the mouse moved away from the user (forward).
#define MOUSE_MAX_DELTA 40

int mouse_invert_y = 0;  // "mouse_invert_y" in the config (m_config.c)
int mouse_invert_x = 0;  // "mouse_invert_x"
int mouse_novert = 0;    // "novert": the mouse only turns
int mouse_scale = 2;     // "mouse_scale": counts to game units, 1..8
extern float mouse_acceleration;  // src/common/i_video.c, as Chocolate Doom's
extern int mouse_threshold;

static int clamp_delta(int v) {
    return v > MOUSE_MAX_DELTA ? MOUSE_MAX_DELTA : v < -MOUSE_MAX_DELTA ? -MOUSE_MAX_DELTA : v;
}

// Chocolate Doom's AccelerateMouse: past the threshold, faster
static int accelerate(int v) {
    if (v < 0) return -accelerate(-v);
    if (mouse_threshold > 0 && v > mouse_threshold)
        return (int)((v - mouse_threshold) * mouse_acceleration + mouse_threshold);
    return v;
}

void DG_PostMouse(int buttons, int dx, int dy, int wheel) {
    (void)wheel;
    event_t ev;
    ev.type = ev_mouse;
    ev.data1 = buttons & 0x07;
    ev.data2 = accelerate(clamp_delta(mouse_invert_x ? -dx : dx) * mouse_scale);
    ev.data3 = mouse_novert ? 0 : accelerate(clamp_delta(mouse_invert_y ? -dy : dy) * mouse_scale);
    ev.data4 = 0;
    D_PostEvent(&ev);
}

static void usb_poll(void) {
    static uint32_t last_us;
    const uint32_t now = time_us_32();
    if (now - last_us < 1000) return;  // at most once a millisecond
    last_us = now;
    usbhid_wrapper_poll();
}

static uint8_t usb_prev_buttons;

static void usb_mouse_tick(void) {
    int dx, dy, wheel;
    uint8_t buttons;
    if (!usbhid_wrapper_get_mouse(&dx, &dy, &wheel, &buttons)) return;
    if (dx || dy || buttons != usb_prev_buttons) {
        DG_PostMouse(buttons, dx, dy, wheel);
        usb_prev_buttons = buttons;
    }
}

//--------------------------------------------------------------------
// Keys made up here: the gamepad's, and releases after the settings
//--------------------------------------------------------------------

#define SYNTH_KEYS 32
static struct { uint8_t pressed, key; } synth[SYNTH_KEYS];
static unsigned synth_wr, synth_rd;

static void synth_key(int pressed, int key) {
    if (synth_wr - synth_rd < SYNTH_KEYS) {
        synth[synth_wr % SYNTH_KEYS].pressed = (uint8_t)pressed;
        synth[synth_wr % SYNTH_KEYS].key = (uint8_t)key;
        synth_wr++;
    }
}

static int synth_pop(int *pressed, unsigned char *key) {
    if (synth_rd == synth_wr) return 0;
    *pressed = synth[synth_rd % SYNTH_KEYS].pressed;
    *key = synth[synth_rd % SYNTH_KEYS].key;
    synth_rd++;
    return 1;
}

//--------------------------------------------------------------------
// Gamepad
//--------------------------------------------------------------------

// The buttons as the games number them (joyb_* in the config)
enum { JB_A, JB_B, JB_X, JB_Y, JB_LB, JB_RB, JB_LT, JB_RT, JB_L3, JB_R3 };

int gamepad_dead_zone = 20;   // "gamepad_dead_zone", percent of full deflection
int gamepad_invert_look = 0;  // "gamepad_invert_look"
int gamepad_swap_sticks = 0;  // "gamepad_swap_sticks": left stick turns, right strafes

// pad buttons -> the keys they stand for, by context
typedef struct { uint16_t bit; uint8_t key; } pad_key_t;

static const pad_key_t pad_game_keys[] = {
    {GP_UP, KEY_UPARROW}, {GP_DOWN, KEY_DOWNARROW}, {GP_LEFT, KEY_LEFTARROW},
    {GP_RIGHT, KEY_RIGHTARROW}, {GP_START, KEY_ESCAPE}, {GP_BACK, KEY_F12},
};
static const pad_key_t pad_menu_keys[] = {  // the game's menu is open
    {GP_UP, KEY_UPARROW}, {GP_DOWN, KEY_DOWNARROW}, {GP_LEFT, KEY_LEFTARROW},
    {GP_RIGHT, KEY_RIGHTARROW}, {GP_START, KEY_ESCAPE}, {GP_BACK, KEY_F12},
    {GP_A, KEY_ENTER}, {GP_RT, KEY_ENTER}, {GP_B, KEY_BACKSPACE}, {GP_Y, 'y'}, {GP_X, 'n'},
};
static const pad_key_t pad_ui_keys[] = {    // the welcome screen and settings
    {GP_UP, KEY_UPARROW}, {GP_DOWN, KEY_DOWNARROW}, {GP_LEFT, KEY_LEFTARROW},
    {GP_RIGHT, KEY_RIGHTARROW}, {GP_A, KEY_ENTER}, {GP_RT, KEY_ENTER}, {GP_START, KEY_ENTER},
    {GP_B, KEY_ESCAPE}, {GP_BACK, KEY_F12}, {GP_X, KEY_BACKSPACE},
    {GP_LB, KEY_PGUP}, {GP_RB, KEY_PGDN},
};

static const pad_key_t *pad_held_map;
static int pad_held_n;
static uint16_t pad_held;  // buttons standing for keys that are down

// Press and release the keys the pad's buttons stand for in this context
static void pad_keys(uint16_t buttons, const pad_key_t *map, int n) {
    if (map != pad_held_map) {  // context changed: let go of the old keys
        for (int i = 0; i < pad_held_n; i++)
            if (pad_held & pad_held_map[i].bit) synth_key(0, pad_held_map[i].key);
        pad_held = 0;
        pad_held_map = map;
        pad_held_n = n;
    }
    uint16_t now = 0;
    for (int i = 0; i < n; i++) {
        const bool down = (buttons & map[i].bit) != 0;
        const bool was = (pad_held & map[i].bit) != 0;
        if (down != was) synth_key(down, map[i].key);
        if (down) now |= map[i].bit;
    }
    pad_held = now;
}

// stick value with the dead zone taken out, as FRACUNIT (65536) at full
static int pad_axis(int v) {
    const int dz = gamepad_dead_zone * 127 / 100;
    if (v > -dz - 1 && v < dz + 1) return 0;
    const int span = 127 - dz;
    const int a = v > 0 ? v - dz : v + dz;
    return span > 0 ? a * 65536 / span : 0;
}

// The pads together: USB (sticks and buttons) and the NES/SNES port. On
// the port, NES A / SNES B fires, NES B / SNES Y uses, SNES A runs, SNES X
// is the automap, L and R change weapons (the USB pad's RT, A, LT, Y, LB,
// RB), Select opens the settings.
static bool get_pad(usbhid_gamepad_state_t *p) {
    bool any = false;
    memset(p, 0, sizeof(*p));
    if (usbhid_wrapper_gamepad_connected()) {
        usbhid_wrapper_get_gamepad(p);
        any = true;
    }
#ifdef NESPAD_GPIO_CLK
    nespad_read();
    const uint32_t n = nespad_state;
    static const struct { uint32_t nes; uint16_t gp; } map[] = {
        {DPAD_UP, GP_UP}, {DPAD_DOWN, GP_DOWN}, {DPAD_LEFT, GP_LEFT}, {DPAD_RIGHT, GP_RIGHT},
        {DPAD_START, GP_START}, {DPAD_SELECT, GP_BACK}, {DPAD_A, GP_RT}, {DPAD_B, GP_A},
        {DPAD_Y, GP_LT}, {DPAD_X, GP_Y}, {DPAD_LT, GP_LB}, {DPAD_RT, GP_RB},
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (n & map[i].nes) p->buttons |= map[i].gp;
    any = true;  // unplugged, the port reads no buttons
#endif
    return any;
}

static usbhid_gamepad_state_t pad_last;
static bool pad_joy_sent;

static void pad_post_joystick(int buttons, int turn, int forward, int strafe, int look) {
    event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ev_joystick;
    ev.data1 = buttons;
    ev.data2 = turn;
    ev.data3 = forward;
    ev.data4 = strafe;
    ev.data5 = look;
    D_PostEvent(&ev);
}

// in the game: the sticks and buttons as the engine's joystick; the D-pad,
// Start and Back as keys
static void pad_game_tick(void) {
    usbhid_gamepad_state_t p;
    if (!get_pad(&p)) {
        if (pad_joy_sent) pad_post_joystick(0, 0, 0, 0, 0);
        pad_joy_sent = false;
        pad_keys(0, pad_held_map ? pad_held_map : pad_game_keys, pad_held_map ? pad_held_n : 0);
        return;
    }
    const fs_glue_t *g = fs_glue_active();
    const bool menu = g && g->menu_active && *g->menu_active;

    if (menu) {
        pad_keys(p.buttons, pad_menu_keys, (int)(sizeof(pad_menu_keys) / sizeof(pad_menu_keys[0])));
        if (pad_joy_sent) pad_post_joystick(0, 0, 0, 0, 0);
        pad_joy_sent = false;
        pad_last = p;
        return;
    }
    pad_keys(p.buttons, pad_game_keys, (int)(sizeof(pad_game_keys) / sizeof(pad_game_keys[0])));

    if (pad_joy_sent && !memcmp(&p, &pad_last, sizeof(p))) return;
    pad_last = p;
    static const struct { uint16_t bit; uint8_t jb; } jbuttons[] = {
        {GP_A, JB_A}, {GP_B, JB_B}, {GP_X, JB_X}, {GP_Y, JB_Y}, {GP_LB, JB_LB}, {GP_RB, JB_RB},
        {GP_LT, JB_LT}, {GP_RT, JB_RT}, {GP_L3, JB_L3}, {GP_R3, JB_R3},
    };
    int jb = 0;
    for (unsigned i = 0; i < sizeof(jbuttons) / sizeof(jbuttons[0]); i++)
        if (p.buttons & jbuttons[i].bit) jb |= 1 << jbuttons[i].jb;
    const int lx = pad_axis(p.lx), ly = pad_axis(p.ly), rx = pad_axis(p.rx), ry = pad_axis(p.ry);
    const int look = gamepad_invert_look ? -ry : ry;
    if (gamepad_swap_sticks) pad_post_joystick(jb, lx, ly, rx, look);
    else pad_post_joystick(jb, rx, ly, lx, look);
    pad_joy_sent = true;
}

// in the firmware UI: the pad as navigation keys (the left stick too)
static void pad_ui_tick(void) {
    uint16_t b = 0;
    usbhid_gamepad_state_t p;
    if (get_pad(&p)) {
        b = p.buttons;
        if (p.ly < -64) b |= GP_UP;
        if (p.ly > 64) b |= GP_DOWN;
        if (p.lx < -64) b |= GP_LEFT;
        if (p.lx > 64) b |= GP_RIGHT;
    }
    pad_keys(b, pad_ui_keys, (int)(sizeof(pad_ui_keys) / sizeof(pad_ui_keys[0])));
}

//--------------------------------------------------------------------
// Keyboard
//--------------------------------------------------------------------

// Keys the engine has seen pressed, released for it when the settings close
static uint32_t keys_down[8];

#if defined(FRANK_UART_CONSOLE) || defined(FRANK_USB_CONSOLE)
// Bench console on UART0 or the native USB: a character is a key (pressed and released),
// Enter, Backspace, Tab, Esc, ANSI arrows / Home / End / PgUp / PgDn /
// Ins / Del / F1..F12. "~BOOT~" reboots into BOOTSEL, "~RST~" restarts.
static void console_tap(int key) {
    synth_key(1, key);
    synth_key(0, key);
}

static void console_poll(void) {
    static int esc;          // 0 none, 1 ESC seen, 2 in "ESC ["
    static int num;
    static uint32_t esc_at;
    static char tail[8];
    int ch;
    while ((ch = getchar_timeout_us(0)) >= 0) {
        memmove(tail, tail + 1, sizeof(tail) - 2);
        tail[sizeof(tail) - 2] = (char)ch;
        if (!strcmp(tail + 1, "~BOOT~")) { printf("\nrebooting to BOOTSEL\n"); sleep_ms(20); platform_reboot(true); }
        if (!strcmp(tail + 2, "~RST~")) { printf("\nrebooting\n"); sleep_ms(20); platform_reboot(false); }
        if (esc == 1) {
            if (ch == '[' || ch == 'O') { esc = 2; num = 0; continue; }
            console_tap(KEY_ESCAPE);
            esc = 0;
        }
        if (esc == 2) {
            if (ch >= '0' && ch <= '9') { num = num * 10 + (ch - '0'); continue; }
            esc = 0;
            static const struct { int n; int key; } tilde[] = {
                {1, KEY_HOME}, {2, KEY_INS}, {3, KEY_DEL}, {4, KEY_END}, {5, KEY_PGUP}, {6, KEY_PGDN},
                {11, KEY_F1}, {12, KEY_F2}, {13, KEY_F3}, {14, KEY_F4}, {15, KEY_F5}, {17, KEY_F6},
                {18, KEY_F7}, {19, KEY_F8}, {20, KEY_F9}, {21, KEY_F10}, {23, KEY_F11}, {24, KEY_F12},
            };
            switch (ch) {
            case 'A': console_tap(KEY_UPARROW); break;
            case 'B': console_tap(KEY_DOWNARROW); break;
            case 'C': console_tap(KEY_RIGHTARROW); break;
            case 'D': console_tap(KEY_LEFTARROW); break;
            case 'H': console_tap(KEY_HOME); break;
            case 'F': console_tap(KEY_END); break;
            case 'P': console_tap(KEY_F1); break;
            case '~':
                for (unsigned i = 0; i < sizeof(tilde) / sizeof(tilde[0]); i++)
                    if (tilde[i].n == num) console_tap(tilde[i].key);
                break;
            }
            continue;
        }
        if (ch == 0x1b) { esc = 1; esc_at = time_us_32(); continue; }
        if (ch == '\r' || ch == '\n') console_tap(KEY_ENTER);
        else if (ch == 0x7f || ch == 8) console_tap(KEY_BACKSPACE);
        else if (ch == '\t') console_tap(KEY_TAB);
        else if (ch >= 'A' && ch <= 'Z') console_tap(ch - 'A' + 'a');
        else if (ch >= 32 && ch < 127) console_tap(ch);
    }
    if (esc == 1 && time_us_32() - esc_at > 30000) {  // a lone ESC
        console_tap(KEY_ESCAPE);
        esc = 0;
    }
}
#endif

// Bench hook: a debugger writes 0x100 | key (press) or 0x200 | key
// (release) here over SWD, the firmware takes it as a keyboard event;
// 0xB007 reboots into BOOTSEL, 0x0857 restarts (for builds without a console).
volatile uint32_t dg_swd_key;

static int raw_key(int *pressed, unsigned char *key) {
    const uint32_t swd = dg_swd_key;
    if (swd == 0xB007 || swd == 0x0857) {
        dg_swd_key = 0;
        platform_reboot(swd == 0xB007);
    }
    if (swd) {
        dg_swd_key = 0;
        *pressed = (swd & 0x100) != 0;
        *key = (unsigned char)swd;
        return 1;
    }
#if defined(FRANK_UART_CONSOLE) || defined(FRANK_USB_CONSOLE)
    console_poll();
#endif
    ps2kbd_tick();
    usb_poll();
    if (ps2kbd_get_key(pressed, key)) return 1;
    if (usbhid_wrapper_get_key(pressed, key)) return 1;
    return synth_pop(pressed, key);
}

#if (defined(FRANK_UART_CONSOLE) || defined(FRANK_NO_PS2_MOUSE)) && (defined(BOARD_M2) || defined(BOARD_Z0))
#define PS2_MOUSE_OFF 1  // its pins belong to the bench (console / probe UART)
#endif

int DG_GetKey(int* pressed, unsigned char* key) {
#ifndef PS2_MOUSE_OFF
    ps2mouse_wrapper_tick();
#endif
    usb_mouse_tick();
    pad_game_tick();

    while (raw_key(pressed, key)) {
        if (in_game && *key == KEY_F12) {
            if (*pressed) settings_requested = true;
            continue;
        }
        if (*pressed) keys_down[*key >> 5] |= 1u << (*key & 31);
        else keys_down[*key >> 5] &= ~(1u << (*key & 31));
        return 1;
    }
    return 0;
}

// Keys for the firmware UI: no events to the engine, mouse input dropped
int DG_UIKey(int *pressed, unsigned char *key) {
    int16_t dx, dy;
    int8_t wheel;
    uint8_t buttons;
#ifndef PS2_MOUSE_OFF
    ps2mouse_get_state(&dx, &dy, &wheel, &buttons);
#else
    (void)dx; (void)dy; (void)wheel;
#endif
    int udx, udy, uwheel;
    usbhid_wrapper_get_mouse(&udx, &udy, &uwheel, &buttons);
    pad_ui_tick();
    fui_cfg_feed();
    return raw_key(pressed, key);
}

// I_StartTic, after the engine's events: the F12 settings window
void DG_CheckSettings(void) {
    if (!settings_requested) return;
    settings_requested = false;
    const uint32_t t0 = to_ms_since_boot(get_absolute_time());
    pad_post_joystick(0, 0, 0, 0, 0);  // nothing held while the game waits
    pad_joy_sent = false;
    fui_settings_run();
    paused_ms += to_ms_since_boot(get_absolute_time()) - t0;
    // the engine gets its key-ups and an idle mouse; the pad's keys are
    // released by the context change
    synth_rd = synth_wr;
    for (int k = 0; k < 256; k++)
        if (keys_down[k >> 5] & (1u << (k & 31))) {
            keys_down[k >> 5] &= ~(1u << (k & 31));
            synth_key(0, k);
        }
    DG_PostMouse(0, 0, 0, 0);
    usb_prev_buttons = 0;
}

void DG_SetWindowTitle(const char * title) {
}

// Hard fault handler - must be before DG_Init
#include "hardware/exception.h"

#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#define CRASH_MAGIC 0xDEAD0FAD

/* frame: the stacked R0..R3, R12, LR, PC, xPSR of the faulting code */
/* the whole picture of the last fault, kept across the reboot for SWD:
 * magic, R0..R3, R12, LR, PC, xPSR, R4..R11, CFSR, BFAR, MMFAR */
uint32_t __uninitialized_ram(crash_dump)[22];

static void __attribute__((used)) hard_fault_report(const uint32_t *frame, const uint32_t *r4_r11) {
    crash_dump[0] = CRASH_MAGIC;
    for (int i = 0; i < 8; i++) crash_dump[1 + i] = frame[i];
    for (int i = 0; i < 8; i++) crash_dump[9 + i] = r4_r11[i];
    crash_dump[17] = *(volatile uint32_t *)0xE000ED28;
    crash_dump[18] = *(volatile uint32_t *)0xE000ED38;
    crash_dump[19] = *(volatile uint32_t *)0xE000ED34;
    // Save crash info to watchdog scratch registers (survive reboot)
    watchdog_hw->scratch[0] = CRASH_MAGIC;
    watchdog_hw->scratch[1] = frame[6];                         // PC
    watchdog_hw->scratch[2] = frame[5];                         // LR
    watchdog_hw->scratch[3] = *(volatile uint32_t *)0xE000ED28; // CFSR
    watchdog_hw->scratch[4] = *(volatile uint32_t *)0xE000ED38; // BFAR
    watchdog_hw->scratch[5] = frame[0];                         // R0

    // Reboot, core 1 stopped and the PSRAM in SPI mode for the boot ROM
    platform_reboot(false);
}

/* the frame is on MSP or PSP, as EXC_RETURN bit 2 says, before any push */
static void __attribute__((naked)) hard_fault_handler(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "push {r4-r11}\n"
        "mov r1, sp\n"
        "b hard_fault_report\n");
}

void install_fault_handler(void) {
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, hard_fault_handler);
}

// Call early in main to check for crash report from previous boot
void check_crash_report(void) {
    if (watchdog_hw->scratch[0] == CRASH_MAGIC) {
        printf("\n*** CRASH REPORT FROM PREVIOUS BOOT ***\n");
        printf("PC=0x%08lx  LR=0x%08lx\n",
               (unsigned long)watchdog_hw->scratch[1],
               (unsigned long)watchdog_hw->scratch[2]);
        printf("CFSR=0x%08lx  BFAR=0x%08lx  R0=0x%08lx\n",
               (unsigned long)watchdog_hw->scratch[3],
               (unsigned long)watchdog_hw->scratch[4],
               (unsigned long)watchdog_hw->scratch[5]);
        // Clear so we don't print again
        watchdog_hw->scratch[0] = 0;
        printf("Use: arm-none-eabi-addr2line -e frank-idtech1.elf 0x%08lx\n",
               (unsigned long)watchdog_hw->scratch[1]);
        printf("***\n\n");
    }
}

// I_System implementations

void I_Error(const char *error, ...) {
    va_list argptr;
    va_start(argptr, error);
    vprintf(error, argptr);
    va_end(argptr);
    printf("\n");
    while(1) tight_loop_contents();
}

void *I_Realloc(void *ptr, size_t size) {
    void *new_ptr = psram_realloc(ptr, size);
    if (size != 0 && new_ptr == NULL) {
        I_Error("I_Realloc: failed on reallocation of %zu bytes", size);
    }
    return new_ptr;
}

// Exit function list — called by I_Quit before reboot
#define MAX_EXIT_FUNCS 16
static void (*exit_funcs[MAX_EXIT_FUNCS])(void);
static int num_exit_funcs = 0;

void I_Quit(void) {
    // Run registered exit functions (e.g. M_SaveDefaults)
    for (int i = 0; i < num_exit_funcs; i++) {
        exit_funcs[i]();
    }
    printf("I_Quit: rebooting to WAD selector...\n");
    platform_reboot(false);  // back to the start screen
}

byte *I_ZoneBase(int *size) {
    // 6MB PERM pool: try 5MB zone first, fall back
    *size = 5 * 1024 * 1024; // 5MB PSRAM for zone (Strife+voices needs more)
    void *ptr = psram_malloc(*size);

    if (!ptr) {
        *size = 4 * 1024 * 1024;
        ptr = psram_malloc(*size);
    }
    if (!ptr) {
        *size = 3 * 1024 * 1024;
        ptr = psram_malloc(*size);
    }
    return (byte *)ptr;
}

void I_AtExit(void (*func)(void), boolean run_on_error) {
    if (num_exit_funcs < MAX_EXIT_FUNCS) {
        exit_funcs[num_exit_funcs++] = func;
    }
}

void I_PrintBanner(const char *msg) {
    printf("%s\n", msg);
}

void I_PrintDivider(void) {
    printf("------------------------------------------------\n");
}

void I_PrintStartupBanner(const char *gamedescription) {
    I_PrintDivider();
    printf("%s\n", gamedescription);
    I_PrintDivider();
}

boolean I_ConsoleStdout(void) {
    return true;
}

// I_Init implementation
void I_InitGraphics(void);
void I_InitTimer(void);

void I_Init(void) {
    I_InitTimer();
    I_InitGraphics();
}

// Missing I_ functions

void I_InitJoystick(void) {}
void I_BindJoystickVariables(void) {}
void I_Tactile(int on, int off, int total) {}

boolean I_GetMemoryValue(unsigned int offset, void *value, int size)
{
    return false;
}

