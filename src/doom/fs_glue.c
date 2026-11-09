// F12 settings window: Doom's settings (src/fui_menu.c)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "doomstat.h"
#include "m_menu.h"
#include "r_main.h"
#include "s_sound.h"
#include "fui.h"

extern int menuactive;
extern int use_analog, joystick_move_sensitivity, joystick_turn_sensitivity;

extern int mouseSensitivity;
extern int snd_channels;

static void apply_sfx(void) { S_SetSfxVolume(sfxVolume * 8); }
static void apply_music(void) { S_SetMusicVolume(musicVolume * 8); }
static void apply_screen(void) { R_SetViewSize(screenblocks, detailLevel); }

const fs_glue_t fs_glue = {
    .game = "DOOM",
    .sfx_volume = &sfxVolume,
    .music_volume = &musicVolume,
    .mouse_sensitivity = &mouseSensitivity,
    .screenblocks = &screenblocks,
    .messages = &showMessages,
    .snd_channels = &snd_channels,
    .detail = &detailLevel,
    .menu_active = (int *)&menuactive,
    .use_analog = &use_analog,
    .joy_move = &joystick_move_sensitivity,
    .joy_turn = &joystick_turn_sensitivity,
    .joy_look = NULL,
    .apply_sfx = apply_sfx,
    .apply_music = apply_music,
    .apply_screen = apply_screen,
};
