// F12 settings window: Hexen's settings (src/fui_menu.c)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "h2def.h"
#include "r_local.h"
#include "s_sound.h"
#include "fui.h"

extern int MenuActive;
extern int use_analog, joystick_move_sensitivity, joystick_turn_sensitivity, joystick_look_sensitivity;

extern int snd_Channels;

// Hexen scales each sound by snd_MaxVolume as it starts it
static void apply_sfx(void) {}
static void apply_music(void) { S_SetMusicVolume(); }
static void apply_screen(void) { R_SetViewSize(screenblocks, detailLevel); }

const fs_glue_t fs_glue = {
    .game = "HEXEN",
    .sfx_volume = &snd_MaxVolume,
    .music_volume = &snd_MusicVolume,
    .mouse_sensitivity = &mouseSensitivity,
    .screenblocks = &screenblocks,
    .messages = &messageson,
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
