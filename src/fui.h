/*
 * frank-idtech1 firmware UI: the welcome screen (WAD selector) and the F12
 * settings window, drawn off screen and shown at vertical blank.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FUI_H_INCLUDED
#define FUI_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#define FUI_W 320
#define FUI_H 240

/* ---- fui_video.c: two PSRAM frame buffers, flipped at vblank ---- */

void fui_video_init(void);           // allocate both buffers, show the first
uint8_t *fui_back(void);             // the hidden buffer (also DG_ScreenBuffer)
uint8_t *fui_front(void);            // the buffer on screen
void fui_dirty(int x, int y, int w, int h);
void fui_dirty_all(void);
void fui_pal(uint8_t i, uint32_t rgb);  // staged; loaded with the next present
void fui_present(void);              // show the back buffer (and staged palette) at
                                     // vblank, then bring the new back up to date
void fui_sync_back(void);            // back := front, after any pending flip
void fui_game_frame(bool palette);   // engine frame in the back buffer: flip, no copy
void fui_wait_back(void);            // the back buffer is free to draw into

/* ---- fui_gfx.c: drawing into the back buffer (marks it dirty) ---- */

#define FUI_FONT_W 6
#define FUI_FONT_H 8

void fui_fill(int x, int y, int w, int h, uint8_t c);
void fui_px(int x, int y, uint8_t c);
int fui_text(int x, int y, const char *s, uint8_t c);            // returns end x
int fui_text_n(int x, int y, const char *s, int max_chars, uint8_t c);
int fui_text_w(const char *s);
void fui_text_center(int x0, int x1, int y, const char *s, uint8_t c);
void fui_text_right(int x1, int y, const char *s, uint8_t c);
// glyphs scaled by `scale`; with shadow >= 0 a +2,+2 shadow first; row_base:
// each scaled pixel row gets its own palette index (row_base + row)
void fui_title(int x0, int y0, const char *s, int scale, int shadow, uint8_t row_base);
// 1 px bevel: raised = lit edge top/left
void fui_bevel(int x, int y, int w, int h, bool raised, uint8_t lt, uint8_t dk);

/* ---- fui_input.c: keys for the firmware UI ---- */

// next key press (Doom key code, with auto-repeat), or -1 after timeout_ms
int fui_key(int timeout_ms);
void fui_key_flush(void);

/* ---- fui_shell.c: the welcome screen ---- */

typedef struct {
    const char *label;
    const char *file;
    const char *tag;
} fui_wad_t;

// the selector; returns the chosen index (the screen stays up for the load).
// autostart >= 0: that WAD starts unless a key is pressed within 3 s;
// notice: a line shown in the error colour, or NULL
int fui_shell_select(const fui_wad_t *list, int n, int autostart, const char *notice);
// an error screen that never returns
void fui_shell_error(const char *title, const char *line1, const char *line2, const char *hint)
    __attribute__((noreturn));

/* ---- fui_menu.c: the F12 settings window ---- */

void fui_settings_run(void);

/* ---- fui_cfg.c: frank-idtech1.ini, the board's own settings ---- */

typedef struct {
    int cpu_mhz;          // 0: the build's
    int psram_mhz;        // 0: the build's
    int shift_x, shift_y; // picture shift, pixels
    int keyboard;         // layout: 0 US, 1 UK, 2 DE, 3 FR
    char autostart[16];   // a WAD to start at boot, "" for the selector
    int audio;            // the sound output, from the next start: FUI_AUDIO_*
} fui_cfg_t;

enum { FUI_AUDIO_I2S = 0, FUI_AUDIO_HDMI = 1 };

extern fui_cfg_t fui_cfg;
extern bool fui_clocks_reverted;  // the last boot with custom clocks failed

bool fui_cfg_boot(void);       // read; apply the clocks (true: remount the SD card)
void fui_cfg_boot_ok(void);    // the welcome screen is up: clocks proven
void fui_cfg_feed(void);       // keep the boot watchdog quiet meanwhile
bool fui_cfg_save(void);
void fui_cfg_apply_shift(void);

/* ---- per-game settings glue (src/<game>/fs_glue.c) ---- */

typedef struct {
    const char *game;
    int *sfx_volume, *music_volume;      // 0..15
    int *mouse_sensitivity;              // 0..9 in the menus
    int *screenblocks;                   // 3..11
    int *messages;                       // NULL: not offered
    int *voice_volume;                   // Strife, 0..15, or NULL
    int *snd_channels;                   // 1..16, from the next start
    int *detail;                         // Doom: 0 high, 1 low, or NULL
    int *show_talk;                      // Strife: dialogue text, or NULL
    void (*apply_sfx)(void);
    void (*apply_music)(void);
    void (*apply_screen)(void);
    void (*apply_voice)(void);
    int *menu_active;                    // the game's own menu is open
    int *use_analog;                     // gamepad: analog sticks
    int *joy_move, *joy_turn, *joy_look; // gamepad sensitivities, look NULL in Doom
} fs_glue_t;

const fs_glue_t *fs_glue_active(void);  // game_dispatch.c, by active_game

#endif
