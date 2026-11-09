// F12 settings window: Strife's settings (src/fui_menu.c)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "doomstat.h"
#include "m_menu.h"
#include "r_main.h"
#include "s_sound.h"
#include "fui.h"

extern int menuactive;
extern int use_analog, joystick_move_sensitivity, joystick_turn_sensitivity, joystick_look_sensitivity;

extern int mouseSensitivity;
extern int snd_channels;
extern int voiceVolume;
extern int dialogshowtext;

static void apply_sfx(void) { S_SetSfxVolume(sfxVolume * 8); }
static void apply_music(void) { S_SetMusicVolume(musicVolume * 8); }
static void apply_screen(void) { R_SetViewSize(screenblocks, detailLevel); }
static void apply_voice(void) { S_SetVoiceVolume(voiceVolume * 8); }

// Strife has no message toggle
const fs_glue_t fs_glue = {
    .game = "STRIFE",
    .sfx_volume = &sfxVolume,
    .music_volume = &musicVolume,
    .mouse_sensitivity = &mouseSensitivity,
    .screenblocks = &screenblocks,
    .messages = NULL,
    .voice_volume = &voiceVolume,
    .snd_channels = &snd_channels,
    .show_talk = &dialogshowtext,
    .menu_active = (int *)&menuactive,
    .use_analog = &use_analog,
    .joy_move = &joystick_move_sensitivity,
    .joy_turn = &joystick_turn_sensitivity,
    .joy_look = &joystick_look_sensitivity,
    .apply_sfx = apply_sfx,
    .apply_music = apply_music,
    .apply_screen = apply_screen,
    .apply_voice = apply_voice,
};
