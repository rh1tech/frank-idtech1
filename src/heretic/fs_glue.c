// F12 settings window: Heretic's settings (src/fui_menu.c)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "doomdef.h"
#include "p_local.h"
#include "r_local.h"
#include "s_sound.h"
#include "fui.h"

extern int MenuActive;
extern int use_analog, joystick_move_sensitivity, joystick_turn_sensitivity, joystick_look_sensitivity;

extern int snd_Channels;

static void apply_sfx(void) { S_SetMaxVolume(true); }
static void apply_music(void) { S_SetMusicVolume(); }
static void apply_screen(void) { R_SetViewSize(screenblocks, detailLevel); }

const fs_glue_t fs_glue = {
    .game = "HERETIC",
    .sfx_volume = &snd_MaxVolume,
    .music_volume = &snd_MusicVolume,
    .mouse_sensitivity = &mouseSensitivity,
    .screenblocks = &screenblocks,
    .messages = (int *)&messageson,
    .snd_channels = &snd_Channels,
    .menu_active = (int *)&MenuActive,
    .use_analog = &use_analog,
    .joy_move = &joystick_move_sensitivity,
    .joy_turn = &joystick_turn_sensitivity,
    .joy_look = &joystick_look_sensitivity,
    .apply_sfx = apply_sfx,
    .apply_music = apply_music,
    .apply_screen = apply_screen,
};
