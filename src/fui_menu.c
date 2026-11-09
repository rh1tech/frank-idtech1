/*
 * The F12 settings window: frank-amiga's prefs editor (a listview of the
 * settings on the left, the selected one's gadget in a group frame on the
 * right, a status line, EasyRequest-style confirmations) in Doom's
 * gunmetal and red. It opens over the frozen game.
 *
 * The window uses palette entries 224..239 only. The game picture around
 * it keeps the game's palette: its pixels in that range are moved to the
 * nearest colour below 224 for as long as the window is open. Every change
 * is drawn into the hidden buffer and shown at vblank, so nothing flickers.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fui.h"
#include "doomkeys.h"
#include "i_video.h"
#include "i_system.h"
#include "m_config.h"
#include "w_wad.h"
#include "z_zone.h"
#include <stdio.h>
#include <string.h>

/* ---- palette (224..239) ---- */

enum {
    U_BODY = 224, U_WELL, U_LT, U_DK, U_TEXT, U_DIM, U_WHITE, U_SEL, U_SEL_UNF, U_AMBER,
    U_TB1, U_TB2, U_TB3, U_KNOB, U_SEL_LT, U_SHADOW,
};

static const uint32_t ui_rgb[16] = {
    0x34343A, /* body: gunmetal */
    0x18181C, /* recessed wells */
    0x9696A0, /* bevel light */
    0x0C0C0E, /* bevel dark */
    0xD6D6D6, /* text */
    0x808088, /* dim text */
    0xFFFFFF,
    0xA81008, /* selection: Doom red */
    0x54140E, /* selection, unfocused */
    0xFFB800, /* amber */
    0xD23214, 0x96180A, 0x500A04, /* title bar, top to bottom */
    0x70707A, /* slider knob */
    0xE65032, /* selection bevel light */
    0x000000,
};

/* ---- layout (frank-amiga's, scaled to 320x240) ---- */

#define WX0 8
#define WY0 12
#define WX1 312
#define WY1 228
#define TB_H 11
#define CRUMB_Y (WY0 + TB_H + 5)
#define LX0 14
#define LX1 142
#define LY0 40
#define LY1 202
#define RX0 148
#define RX1 306
#define ROW_H 11
#define SY0 207
#define SY1 223

/* ---- the settings model ---- */

enum { K_SUB, K_INT, K_BOOL, K_RADIO, K_ACTION, K_KEY };

typedef struct node {
    const char *label;
    uint8_t kind;
    int lo, hi;
    const char *const *opts;     /* K_RADIO: one per value lo..hi */
    const struct node *kids;     /* K_SUB */
    int nkids;
    bool (*visible)(void);
    int *var;                    /* the setting itself, or get/set */
    int (*get)(void);
    void (*set)(int v);
    void (*run)(void);           /* K_ACTION */
    const char *help;
} node_t;

/* src/doomgeneric_rp2350.c, src/pico/i_picosound.c, src/common/m_controls.c */
extern int mouse_invert_y, mouse_novert, master_volume;
extern int joybspeed, dclick_use;
extern int key_up, key_down, key_left, key_right, key_strafeleft, key_straferight;
extern int key_fire, key_use, key_strafe, key_speed;
extern int key_flyup, key_flydown, key_flycenter, key_lookup, key_lookdown, key_lookcenter;
extern int key_invleft, key_invright, key_useartifact, key_jump;
extern int key_usehealth, key_invquery, key_mission, key_invuse, key_invdrop;
extern int key_prevweapon, key_nextweapon, key_map_toggle, key_pause;
extern int key_weapon1, key_weapon2, key_weapon3, key_weapon4;
extern int key_weapon5, key_weapon6, key_weapon7, key_weapon8;
extern int mousebfire, mousebstrafe, mousebforward, mousebspeed, mousebuse, mousebbackward;
extern int mousebjump, mousebprevweapon, mousebnextweapon;
extern int mousebinvleft, mousebinvright, mousebuseartifact;
extern int active_game;      /* 0 doom, 1 heretic, 2 hexen, 3 strife */
extern int mouse_invert_x, mouse_scale, mouse_threshold;
extern float mouse_acceleration;
extern int audio_stereo, show_fps, keyboard_layout;
extern int gamepad_dead_zone, gamepad_invert_look, gamepad_swap_sticks;
extern int key_arti_quartz, key_arti_urn, key_arti_bomb, key_arti_tome, key_arti_ring;
extern int key_arti_chaosdevice, key_arti_shadowsphere, key_arti_wings, key_arti_torch, key_arti_morph;
extern int key_arti_all, key_arti_health, key_arti_poisonbag, key_arti_blastradius;
extern int key_arti_teleport, key_arti_teleportother, key_arti_egg, key_arti_invulnerability;
extern int key_map_zoomin, key_map_zoomout, key_map_maxzoom, key_map_follow, key_map_grid;
extern int key_map_mark, key_map_clearmark;
extern int key_menu_help, key_menu_save, key_menu_load, key_menu_volume, key_menu_detail;
extern int key_menu_qsave, key_menu_endgame, key_menu_messages, key_menu_qload, key_menu_quit;
extern int key_menu_gamma, key_menu_incscreen, key_menu_decscreen;
extern int joybfire, joybuse, joybstrafe, joybstrafeleft, joybstraferight, joybprevweapon;
extern int joybnextweapon, joybautomap, joybjump, joybuseartifact, joybinvleft, joybinvright;
extern int joybflyup, joybflydown, joybflycenter;
extern const char *dg_current_wad;  /* src/doomgeneric_rp2350.c */

#define ALWAYS_RUN 29        /* joyb_speed >= the joystick buttons: always run */
#define JB_RUN_DEFAULT 6     /* LT, src/common/m_controls.c */

static const fs_glue_t *G;
static char status_msg[32];
static bool quit_now;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int node_get(const node_t *n) {
    const int v = n->get ? n->get() : *n->var;
    return n->kind == K_KEY ? v : n->kind == K_BOOL ? v != 0 : clampi(v, n->lo, n->hi);
}

static void node_set(const node_t *n, int v) {
    if (n->set) n->set(v);
    else *n->var = v;
}

/* which game is running */
static bool g_heretic_hexen(void) { return active_game == 1 || active_game == 2; }
static bool g_hexen_strife(void) { return active_game == 2 || active_game == 3; }
static bool g_look(void) { return active_game != 0; }
static bool g_strife(void) { return active_game == 3; }
static bool g_doom(void) { return active_game == 0; }
static bool g_inventory(void) { return active_game != 0; }

static int get_sfx(void) { return *G->sfx_volume; }
static void set_sfx(int v) { *G->sfx_volume = v; G->apply_sfx(); }
static int get_music(void) { return *G->music_volume; }
static void set_music(int v) { *G->music_volume = v; G->apply_music(); }
static bool has_voice(void) { return G->voice_volume != NULL; }
static int get_voice(void) { return *G->voice_volume; }
static void set_voice(int v) { *G->voice_volume = v; G->apply_voice(); }
static int get_channels(void) { return *G->snd_channels; }
static void set_channels(int v) { *G->snd_channels = v; }
static int get_sens(void) { return *G->mouse_sensitivity; }
static void set_sens(int v) { *G->mouse_sensitivity = v; }
static int get_run(void) { return joybspeed >= ALWAYS_RUN; }
static void set_run(int v) { joybspeed = v ? ALWAYS_RUN : JB_RUN_DEFAULT; }
static int get_screen(void) { return *G->screenblocks; }
static void set_screen(int v) { *G->screenblocks = v; G->apply_screen(); }
static bool has_detail(void) { return G->detail != NULL; }
static int get_detail(void) { return *G->detail; }
static void set_detail(int v) { *G->detail = v; G->apply_screen(); }
static void set_gamma(int v) {
    usegamma = v;
    I_SetPalette((byte *)W_CacheLumpName("PLAYPAL", PU_CACHE));
}
static bool has_messages(void) { return G->messages != NULL; }
static int get_messages(void) { return *G->messages; }
static void set_messages(int v) { *G->messages = v; }
static bool has_talk(void) { return G->show_talk != NULL; }
static int get_talk(void) { return *G->show_talk; }
static void set_talk(int v) { *G->show_talk = v; }

static bool has_analog(void) { return G->use_analog != NULL; }
static int get_analog(void) { return *G->use_analog; }
static void set_analog(int v) { *G->use_analog = v; }
static int get_jturn(void) { return *G->joy_turn; }
static void set_jturn(int v) { *G->joy_turn = v; }
static int get_jmove(void) { return *G->joy_move; }
static void set_jmove(int v) { *G->joy_move = v; }
static bool has_jlook(void) { return G->joy_look != NULL; }
static int get_jlook(void) { return *G->joy_look; }
static void set_jlook(int v) { *G->joy_look = v; }
static bool no_always_run(void) { return joybspeed < ALWAYS_RUN; }
static int get_accel(void) { return (int)(mouse_acceleration * 10.0f + 0.5f); }
static void set_accel(int v) { mouse_acceleration = (float)v / 10.0f; }
static int get_layout(void) { return fui_cfg.keyboard; }
static void set_layout(int v) { fui_cfg.keyboard = keyboard_layout = v; }
static int get_shift_x(void) { return fui_cfg.shift_x; }
static void set_shift_x(int v) { fui_cfg.shift_x = v; fui_cfg_apply_shift(); }
static int get_shift_y(void) { return fui_cfg.shift_y; }
static void set_shift_y(int v) { fui_cfg.shift_y = v; fui_cfg_apply_shift(); }

static const int cpu_mhz[] = {0, 252, 504};  // HDMI: multiples of 252 MHz
static const int psram_mhz[] = {0, 84, 100, 133, 166};
static int index_of(const int *a, int n, int v) {
    for (int i = 0; i < n; i++) if (a[i] == v) return i;
    return 0;
}
static int get_cpu(void) { return index_of(cpu_mhz, 3, fui_cfg.cpu_mhz); }
static void set_cpu(int v) {
    fui_cfg.cpu_mhz = cpu_mhz[v];
    snprintf(status_msg, sizeof(status_msg), "Save, then restart");
}
static int get_psram(void) { return index_of(psram_mhz, 5, fui_cfg.psram_mhz); }
static void set_psram(int v) {
    fui_cfg.psram_mhz = psram_mhz[v];
    snprintf(status_msg, sizeof(status_msg), "Save, then restart");
}
static int get_audio(void) { return fui_cfg.audio; }
static void set_audio(int v) {
    fui_cfg.audio = v;
    snprintf(status_msg, sizeof(status_msg), "Save, then restart");
}
static int get_autostart(void) { return dg_current_wad[0] && !strcmp(fui_cfg.autostart, dg_current_wad); }
static void set_autostart(int v) {
    snprintf(fui_cfg.autostart, sizeof(fui_cfg.autostart), "%s", v ? dg_current_wad : "");
}

static void do_save(void) {
    M_SaveDefaults();
    const bool ok = fui_cfg_save();
    snprintf(status_msg, sizeof(status_msg), ok ? "Saved to the card" : "Card write failed");
}

static bool confirm(const char *title, const char *text);

static void do_quit(void) {
    if (confirm("Quit", "Quit to the WAD selector?")) quit_now = true;
}

static const char *const gamma_opts[] = {"Off", "Level 1", "Level 2", "Level 3", "Level 4"};
static const char *const detail_opts[] = {"High", "Low"};
static const char *const mouseb_opts[] = {"None", "Left", "Right", "Middle"};
static const char *const joyb_opts[] = {"None", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "L3", "R3"};
static const char *const stereo_opts[] = {"Stereo", "Swapped", "Mono"};
static const char *const layout_opts[] = {"US", "UK", "German", "French"};
static const char *const cpu_opts[] = {"Build", "252 MHz", "504 MHz"};
static const char *const audio_opts[] = {"I2S", "HDMI"};
static const char *const psram_opts[] = {"Build", "84 MHz", "100 MHz", "133 MHz", "166 MHz"};

#define KEY(l, v, vis) {l, K_KEY, .var = &v, .visible = vis, .help = "Enter, then press the\nnew key. Esc cancels."}
#define MOUSEB(l, v, vis) {l, K_RADIO, -1, 2, .opts = mouseb_opts, .var = &v, .visible = vis, \
                           .help = "The mouse button for\nthis action."}
#define JOYB(l, v, vis) {l, K_RADIO, -1, 9, .opts = joyb_opts, .var = &v, .visible = vis, \
                         .help = "The gamepad button."}
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const node_t audio_kids[] = {
    {"Master volume", K_INT, 0, 16, .var = &master_volume,
     .help = "Everything the board\nplays.\nLeft/Right to adjust."},
    {"SFX volume", K_INT, 0, 15, .get = get_sfx, .set = set_sfx,
     .help = "Sound effects.\nLeft/Right to adjust."},
    {"Music volume", K_INT, 0, 15, .get = get_music, .set = set_music,
     .help = "OPL music.\nLeft/Right to adjust."},
    {"Voice volume", K_INT, 0, 15, .visible = has_voice, .get = get_voice, .set = set_voice,
     .help = "Strife's spoken\ndialogue."},
    {"Channels", K_INT, 1, 16, .get = get_channels, .set = set_channels,
     .help = "Sounds that play at\nonce. From the next\ngame start (save\nfirst)."},
    {"Device", K_RADIO, 0, 1, .opts = audio_opts, .get = get_audio, .set = set_audio,
     .help = "I2S: the DAC module.\nHDMI: the TV, 44.1 kHz.\nFrom the next start\n(save first)."},
    {"Output", K_RADIO, 0, 2, .opts = stereo_opts, .var = &audio_stereo,
     .help = "Swapped: left and\nright change places.\nMono: both the same."},
};

static const node_t controls_kids[] = {
    {"Mouse speed", K_INT, 0, 9, .get = get_sens, .set = set_sens,
     .help = "The game's mouse\nsensitivity."},
    {"Mouse scale", K_INT, 1, 8, .var = &mouse_scale,
     .help = "Mouse counts per game\nunit: for mice too\nfast or too slow at\nany speed."},
    {"Acceleration", K_INT, 10, 40, .get = get_accel, .set = set_accel,
     .help = "Fast movements count\nmore: 10 is off, 20\ndoubles them."},
    {"Accel. from", K_INT, 0, 32, .var = &mouse_threshold,
     .help = "How fast the mouse\nmust move before\nacceleration starts."},
    {"Invert X", K_BOOL, .var = &mouse_invert_x,
     .help = "Turn the other way."},
    {"Invert Y", K_BOOL, .var = &mouse_invert_y,
     .help = "Pull back to walk\nforward."},
    {"No vertical", K_BOOL, .var = &mouse_novert,
     .help = "Ignore forward and\nback mouse movement:\nthe mouse only turns."},
    {"Always run", K_BOOL, .get = get_run, .set = set_run,
     .help = "Shift then walks."},
    {"Double-click", K_BOOL, .var = &dclick_use,
     .help = "Double-click the\nforward or strafe\nbutton to use."},
    {"Keyboard", K_RADIO, 0, 3, .opts = layout_opts, .get = get_layout, .set = set_layout,
     .help = "The keyboard's layout\n(the digit row stays\ndigits)."},
};

static const node_t keys_kids[] = {
    KEY("Forward", key_up, NULL),
    KEY("Backward", key_down, NULL),
    KEY("Turn left", key_left, NULL),
    KEY("Turn right", key_right, NULL),
    KEY("Strafe left", key_strafeleft, NULL),
    KEY("Strafe right", key_straferight, NULL),
    KEY("Fire", key_fire, NULL),
    KEY("Use", key_use, NULL),
    KEY("Strafe on", key_strafe, NULL),
    KEY("Run", key_speed, NULL),
    KEY("Jump", key_jump, g_hexen_strife),
    KEY("Fly up", key_flyup, g_heretic_hexen),
    KEY("Fly down", key_flydown, g_heretic_hexen),
    KEY("Fly center", key_flycenter, g_heretic_hexen),
    KEY("Look up", key_lookup, g_look),
    KEY("Look down", key_lookdown, g_look),
    KEY("Look center", key_lookcenter, g_heretic_hexen),
    KEY("Inv left", key_invleft, g_inventory),
    KEY("Inv right", key_invright, g_inventory),
    KEY("Use artifact", key_useartifact, g_heretic_hexen),
    KEY("Use item", key_invuse, g_strife),
    KEY("Drop item", key_invdrop, g_strife),
    KEY("Use health", key_usehealth, g_strife),
    KEY("Inv query", key_invquery, g_strife),
    KEY("Mission", key_mission, g_strife),
    KEY("Prev weapon", key_prevweapon, NULL),
    KEY("Next weapon", key_nextweapon, NULL),
    KEY("Weapon 1", key_weapon1, NULL),
    KEY("Weapon 2", key_weapon2, NULL),
    KEY("Weapon 3", key_weapon3, NULL),
    KEY("Weapon 4", key_weapon4, NULL),
    KEY("Weapon 5", key_weapon5, NULL),
    KEY("Weapon 6", key_weapon6, NULL),
    KEY("Weapon 7", key_weapon7, NULL),
    KEY("Weapon 8", key_weapon8, g_doom),
    KEY("Pause", key_pause, NULL),
};

static bool g_heretic(void) { return active_game == 1; }
static bool g_hexen(void) { return active_game == 2; }

static const node_t arti_kids[] = {
    KEY("Quartz flask", key_arti_quartz, g_heretic),
    KEY("Mystic urn", key_arti_urn, g_heretic),
    KEY("Time bomb", key_arti_bomb, g_heretic),
    KEY("Tome of power", key_arti_tome, g_heretic),
    KEY("Invulnerable", key_arti_ring, g_heretic),
    KEY("Chaos device", key_arti_chaosdevice, g_heretic),
    KEY("Shadowsphere", key_arti_shadowsphere, g_heretic),
    KEY("Wings", key_arti_wings, g_heretic),
    KEY("Torch", key_arti_torch, g_heretic),
    KEY("Morph ovum", key_arti_morph, g_heretic),
    KEY("Use all", key_arti_all, g_hexen),
    KEY("Quartz flask", key_arti_health, g_hexen),
    KEY("Flechette", key_arti_poisonbag, g_hexen),
    KEY("Repulsion", key_arti_blastradius, g_hexen),
    KEY("Chaos device", key_arti_teleport, g_hexen),
    KEY("Banishment", key_arti_teleportother, g_hexen),
    KEY("Porkalator", key_arti_egg, g_hexen),
    KEY("Defender", key_arti_invulnerability, g_hexen),
};

static const node_t map_kids[] = {
    KEY("Automap", key_map_toggle, NULL),
    KEY("Zoom in", key_map_zoomin, NULL),
    KEY("Zoom out", key_map_zoomout, NULL),
    KEY("Whole map", key_map_maxzoom, NULL),
    KEY("Follow", key_map_follow, NULL),
    KEY("Grid", key_map_grid, NULL),
    KEY("Mark", key_map_mark, NULL),
    KEY("Clear marks", key_map_clearmark, NULL),
};

static const node_t menukey_kids[] = {
    KEY("Help", key_menu_help, NULL),
    KEY("Save game", key_menu_save, NULL),
    KEY("Load game", key_menu_load, NULL),
    KEY("Volume", key_menu_volume, NULL),
    KEY("Detail", key_menu_detail, g_doom),
    KEY("Quick save", key_menu_qsave, NULL),
    KEY("End game", key_menu_endgame, NULL),
    KEY("Messages", key_menu_messages, NULL),
    KEY("Quick load", key_menu_qload, NULL),
    KEY("Quit", key_menu_quit, NULL),
    KEY("Gamma", key_menu_gamma, NULL),
    KEY("Bigger view", key_menu_incscreen, NULL),
    KEY("Smaller view", key_menu_decscreen, NULL),
};

/* every key binding, for swapping a key that is taken */
static const struct { const node_t *n; int count; } key_pages[] = {
    {keys_kids, COUNT(keys_kids)}, {arti_kids, COUNT(arti_kids)},
    {map_kids, COUNT(map_kids)}, {menukey_kids, COUNT(menukey_kids)},
};

static const node_t mouseb_kids[] = {
    MOUSEB("Fire", mousebfire, NULL),
    MOUSEB("Strafe on", mousebstrafe, NULL),
    MOUSEB("Forward", mousebforward, NULL),
    MOUSEB("Backward", mousebbackward, NULL),
    MOUSEB("Run", mousebspeed, NULL),
    MOUSEB("Use", mousebuse, NULL),
    MOUSEB("Jump", mousebjump, g_hexen_strife),
    MOUSEB("Prev weapon", mousebprevweapon, NULL),
    MOUSEB("Next weapon", mousebnextweapon, NULL),
    MOUSEB("Inv left", mousebinvleft, g_heretic_hexen),
    MOUSEB("Inv right", mousebinvright, g_heretic_hexen),
    MOUSEB("Use artifact", mousebuseartifact, g_heretic_hexen),
};

static const node_t joyb_kids[] = {
    JOYB("Fire", joybfire, NULL),
    JOYB("Use", joybuse, NULL),
    JOYB("Run", joybspeed, no_always_run),
    JOYB("Strafe on", joybstrafe, NULL),
    JOYB("Strafe left", joybstrafeleft, NULL),
    JOYB("Strafe right", joybstraferight, NULL),
    JOYB("Prev weapon", joybprevweapon, NULL),
    JOYB("Next weapon", joybnextweapon, NULL),
    JOYB("Automap", joybautomap, NULL),
    JOYB("Jump", joybjump, g_hexen_strife),
    JOYB("Use artifact", joybuseartifact, g_heretic_hexen),
    JOYB("Inv left", joybinvleft, g_heretic_hexen),
    JOYB("Inv right", joybinvright, g_heretic_hexen),
    JOYB("Fly up", joybflyup, g_heretic_hexen),
    JOYB("Fly down", joybflydown, g_heretic_hexen),
    JOYB("Fly center", joybflycenter, g_heretic_hexen),
};

static const node_t pad_kids[] = {
    {"Buttons", K_SUB, .kids = joyb_kids, .nkids = COUNT(joyb_kids),
     .help = "Start: the game's\nmenu. Back: this\nwindow. D-pad: the\narrow keys."},
    {"Analog", K_BOOL, .visible = has_analog, .get = get_analog, .set = set_analog,
     .help = "Off: the sticks act\nlike the D-pad."},
    {"Turn speed", K_INT, 0, 20, .get = get_jturn, .set = set_jturn,
     .help = "Right stick, left and\nright."},
    {"Move speed", K_INT, 0, 20, .get = get_jmove, .set = set_jmove,
     .help = "Left stick."},
    {"Look speed", K_INT, 0, 20, .visible = has_jlook, .get = get_jlook, .set = set_jlook,
     .help = "Right stick, up and\ndown."},
    {"Dead zone", K_INT, 0, 50, .var = &gamepad_dead_zone,
     .help = "Percent of the stick's\ntravel that does\nnothing."},
    {"Invert look", K_BOOL, .visible = g_look, .var = &gamepad_invert_look,
     .help = "Push up to look down."},
    {"Swap sticks", K_BOOL, .var = &gamepad_swap_sticks,
     .help = "Left stick turns,\nright stick strafes."},
};

static const node_t display_kids[] = {
    {"Screen size", K_INT, 3, 11, .get = get_screen, .set = set_screen,
     .help = "The view size, as in\nthe game's options."},
    {"Detail", K_RADIO, 0, 1, .opts = detail_opts, .visible = has_detail, .get = get_detail,
     .set = set_detail, .help = "Low: half the\nhorizontal detail,\nfaster."},
    {"Gamma", K_RADIO, 0, 4, .opts = gamma_opts, .var = &usegamma, .set = set_gamma,
     .help = "Brightness (F11 in\nthe game)."},
    {"Messages", K_BOOL, .visible = has_messages, .get = get_messages, .set = set_messages,
     .help = "Pickup and other\nmessages."},
    {"Dialog text", K_BOOL, .visible = has_talk, .get = get_talk, .set = set_talk,
     .help = "Show the text of\nwhat people say."},
    {"FPS counter", K_BOOL, .var = &show_fps,
     .help = "Frames per second in\nthe top right."},
    {"Shift X", K_INT, -32, 32, .get = get_shift_x, .set = set_shift_x,
     .help = "Move the picture for\na TV that cuts the\nedges off."},
    {"Shift Y", K_INT, -32, 32, .get = get_shift_y, .set = set_shift_y,
     .help = "Move the picture up\nor down."},
};

static const node_t system_kids[] = {
    {"CPU", K_RADIO, 0, 2, .opts = cpu_opts, .get = get_cpu, .set = set_cpu,
     .help = "From the next start.\nIf it fails to start,\nthe build's comes\nback by itself."},
    {"PSRAM", K_RADIO, 0, 4, .opts = psram_opts, .get = get_psram, .set = set_psram,
     .help = "From the next start.\nToo fast: crashes,\nthen the build's\nspeed comes back."},
    {"Start at boot", K_BOOL, .get = get_autostart, .set = set_autostart,
     .help = "Start this WAD at\nboot; a key in the\nfirst 3 s shows the\nlist."},
};

static const node_t root_kids[] = {
    {"Audio", K_SUB, .kids = audio_kids, .nkids = COUNT(audio_kids)},
    {"Controls", K_SUB, .kids = controls_kids, .nkids = COUNT(controls_kids)},
    {"Keys", K_SUB, .kids = keys_kids, .nkids = COUNT(keys_kids)},
    {"Artifact keys", K_SUB, .kids = arti_kids, .nkids = COUNT(arti_kids), .visible = g_heretic_hexen},
    {"Automap keys", K_SUB, .kids = map_kids, .nkids = COUNT(map_kids)},
    {"Menu keys", K_SUB, .kids = menukey_kids, .nkids = COUNT(menukey_kids)},
    {"Mouse buttons", K_SUB, .kids = mouseb_kids, .nkids = COUNT(mouseb_kids)},
    {"Gamepad", K_SUB, .kids = pad_kids, .nkids = COUNT(pad_kids)},
    {"Display", K_SUB, .kids = display_kids, .nkids = COUNT(display_kids)},
    {"System", K_SUB, .kids = system_kids, .nkids = COUNT(system_kids)},
    {"Save settings", K_ACTION, .run = do_save,
     .help = "Write the settings to\nthe game's config file\nand frank-idtech1.ini\non the SD card."},
    {"Quit to WADs", K_ACTION, .run = do_quit,
     .help = "Leave the game and go\nback to the WAD\nselector (restarts)."},
};

static const node_t root = {"Settings", K_SUB, .kids = root_kids, .nkids = COUNT(root_kids)};

#define MAX_ROWS 40

/* rows of a submenu, without the hidden ones */
static int rows_of(const node_t *n, const node_t **out) {
    int c = 0;
    for (int i = 0; i < n->nkids && c < MAX_ROWS; i++)
        if (!n->kids[i].visible || n->kids[i].visible()) out[c++] = &n->kids[i];
    return c;
}

static const char *key_name(int key) {
    static const struct { int key; const char *name; } names[] = KEY_NAMES_ARRAY;
    static char buf[8];
    if (key == 0) return "---";
    for (int i = 0; i < COUNT(names); i++)
        if (names[i].key == key) return names[i].name;
    if (key > 32 && key < 127) snprintf(buf, sizeof(buf), "%c", key);
    else snprintf(buf, sizeof(buf), "#%d", key);
    return buf;
}

static void value_text(const node_t *n, char *buf, size_t len) {
    buf[0] = 0;
    switch (n->kind) {
    case K_INT: snprintf(buf, len, "%d", node_get(n)); break;
    case K_BOOL: snprintf(buf, len, "%s", node_get(n) ? "On" : "Off"); break;
    case K_RADIO: snprintf(buf, len, "%s", n->opts[node_get(n) - n->lo]); break;
    case K_KEY: snprintf(buf, len, "%s", key_name(node_get(n))); break;
    case K_SUB: snprintf(buf, len, ">"); break;
    default: break;
    }
}

/* ---- navigation state ---- */

#define MAX_DEPTH 4
#define LIST_ROWS ((LY1 - LY0 - 4) / ROW_H)
static const node_t *stack[MAX_DEPTH];
static int sel_stack[MAX_DEPTH], top_stack[MAX_DEPTH];
static int depth;
static const node_t *capturing;   /* K_KEY waiting for its key */

/* ---- drawing ---- */

static void title_bar(int x0, int y0, int x1, const char *title, const char *right) {
    for (int r = 0; r < TB_H; r++)
        fui_fill(x0, y0 + r, x1 - x0, 1, r < 4 ? U_TB1 : r < 8 ? U_TB2 : U_TB3);
    /* close gadget */
    fui_bevel(x0 + 2, y0 + 1, 11, TB_H - 2, true, U_SEL_LT, U_TB3);
    fui_fill(x0 + 6, y0 + 4, 3, 3, U_DK);
    fui_text(x0 + 18, y0 + 2, title, U_AMBER);
    if (right) fui_text_right(x1 - 5, y0 + 2, right, U_TEXT);
}

static void rivet(int x, int y) {
    fui_fill(x, y, 2, 2, U_DK);
    fui_px(x, y, U_LT);
}

static void window(int x0, int y0, int x1, int y1, const char *title, const char *right) {
    fui_fill(x0 + 3, y0 + 3, x1 - x0, y1 - y0, U_SHADOW);   /* drop shadow */
    fui_fill(x0, y0, x1 - x0, y1 - y0, U_BODY);
    fui_bevel(x0, y0, x1 - x0, y1 - y0, true, U_LT, U_DK);
    title_bar(x0 + 1, y0 + 1, x1 - 1, title, right);
    rivet(x0 + 3, y1 - 4);
    rivet(x1 - 5, y1 - 4);
}

/* a recessed box, or a ridge (recessed, then raised inside) */
static void well(int x0, int y0, int x1, int y1, uint8_t fill) {
    fui_bevel(x0, y0, x1 - x0, y1 - y0, false, U_LT, U_DK);
    fui_fill(x0 + 1, y0 + 1, x1 - x0 - 2, y1 - y0 - 2, fill);
}

static void ridge(int x0, int y0, int x1, int y1, const char *title) {
    fui_bevel(x0, y0, x1 - x0, y1 - y0, false, U_LT, U_DK);
    fui_bevel(x0 + 1, y0 + 1, x1 - x0 - 2, y1 - y0 - 2, true, U_LT, U_DK);
    if (title) {
        const int w = fui_text_w(title);
        fui_fill(x0 + 7, y0 - 4, w + 4, FUI_FONT_H, U_BODY);
        fui_text(x0 + 9, y0 - 4, title, U_TEXT);
    }
}

static void mx_knob(int x, int y, bool on) {
    fui_bevel(x, y, 13, 9, !on, U_LT, U_DK);
    fui_fill(x + 1, y + 1, 11, 7, on ? U_WELL : U_BODY);
    if (on) fui_fill(x + 4, y + 3, 5, 3, U_SEL_LT);
}

static void button(int cx, int y, const char *label, bool selected) {
    int w = fui_text_w(label) + 24;
    if (w < 56) w = 56;
    const int x = cx - w / 2;
    fui_fill(x, y, w, 15, selected ? U_SEL : U_BODY);
    fui_bevel(x, y, w, 15, !selected, selected ? U_SEL_LT : U_LT, U_DK);
    fui_text_center(x, x + w, y + 4, label, selected ? U_WHITE : U_TEXT);
}

static void help_text(int x, int y, const char *s) {
    char line[32];
    while (s && *s) {
        const char *e = strchr(s, '\n');
        const size_t n = e ? (size_t)(e - s) : strlen(s);
        snprintf(line, sizeof(line), "%.*s", (int)n, s);
        if (y + FUI_FONT_H > LY1 - 3) break;  /* the group frame's bottom */
        fui_text(x, y, line, U_DIM);
        y += 10;
        s = e ? e + 1 : NULL;
    }
}

static void draw_right(const node_t *n) {
    const int x0 = RX0, y0 = LY0 + 4, x1 = RX1, y1 = LY1;
    char buf[32];
    ridge(x0, y0, x1, y1, n->label);
    const int ix = x0 + 10;
    int y = y0 + 12;

    switch (n->kind) {
    case K_INT: {
        const int tx0 = ix, tx1 = x1 - 10, ty = y + 4, kw = 14;
        well(tx0, ty, tx1, ty + 11, U_WELL);
        const int span = tx1 - tx0 - 2 - kw;
        const int v = node_get(n);
        const int kx = tx0 + 1 + (n->hi > n->lo ? span * (v - n->lo) / (n->hi - n->lo) : 0);
        fui_fill(tx0 + 1, ty + 1, kx - tx0 - 1, 9, U_SEL_UNF);  /* the filled part */
        fui_fill(kx, ty + 1, kw, 9, U_KNOB);
        fui_bevel(kx, ty + 1, kw, 9, true, U_LT, U_DK);
        snprintf(buf, sizeof(buf), "%d", n->lo);
        fui_text(tx0, ty + 15, buf, U_DIM);
        snprintf(buf, sizeof(buf), "%d", n->hi);
        fui_text_right(tx1, ty + 15, buf, U_DIM);
        snprintf(buf, sizeof(buf), "%d", v);
        fui_text_center(tx0, tx1, ty + 15, buf, U_AMBER);
        y = ty + 34;
        break;
    }
    case K_BOOL:
    case K_RADIO: {
        static const char *const bool_opts[] = {"Off", "On"};
        const char *const *opts = n->kind == K_BOOL ? bool_opts : n->opts;
        const int lo = n->kind == K_BOOL ? 0 : n->lo;
        const int count = n->kind == K_BOOL ? 2 : n->hi - n->lo + 1;
        const int v = node_get(n);
        const int step = count > 6 ? 11 : 13;
        for (int i = 0; i < count; i++, y += step) {
            mx_knob(ix, y, v == lo + i);
            fui_text(ix + 20, y + 1, opts[i], v == lo + i ? U_WHITE : U_TEXT);
        }
        y += 6;
        break;
    }
    case K_KEY: {
        /* the key as a raised keycap; recessed and red while waiting for one */
        const bool wait = capturing == n;
        const char *name = wait ? "?" : key_name(node_get(n));
        int w = fui_text_w(name) + 16;
        if (w < 40) w = 40;
        const int kx = (x0 + x1 - w) / 2;
        fui_fill(kx, y + 2, w, 19, wait ? U_SEL : U_KNOB);
        fui_bevel(kx, y + 2, w, 19, !wait, wait ? U_SEL_LT : U_LT, U_DK);
        fui_text_center(kx, kx + w, y + 8, name, U_WHITE);
        y += 32;
        if (wait) {
            fui_text(ix, y, "Press the new key.", U_AMBER);
            fui_text(ix, y + 10, "Esc cancels.", U_DIM);
            return;
        }
        break;
    }
    case K_SUB: {
        const node_t *rows[MAX_ROWS];
        const int c = rows_of(n, rows);
        const int fit = (y1 - 8 - y) / 11;
        for (int i = 0; i < c && i < fit; i++, y += 11) {
            fui_text(ix, y, (i == fit - 1 && c > fit) ? "..." : rows[i]->label, U_TEXT);
            if (i == fit - 1 && c > fit) break;
            value_text(rows[i], buf, sizeof(buf));
            fui_text_right(x1 - 10, y, buf, U_AMBER);
        }
        y += 6;
        break;
    }
    case K_ACTION:
        button((x0 + x1) / 2, y + 2, n->label, true);
        y += 28;
        break;
    }
    help_text(ix, y, n->help);
}

static void draw_status(const node_t *n) {
    well(LX0, SY0, RX1, SY1, U_BODY);
    const char *h = "Up/Dn Move  Enter Open  Esc Close";
    switch (n->kind) {
    case K_INT: h = "Left/Right Adjust  Esc Back"; break;
    case K_BOOL: case K_RADIO: h = "Left/Right/Enter Change  Esc Back"; break;
    case K_KEY: h = capturing ? "Press a key  Esc Cancel" : "Enter Change key  Esc Back"; break;
    case K_ACTION: h = "Enter Run  Esc Close"; break;
    default: if (depth > 0) h = "Up/Dn Move  Enter Open  Esc Back"; break;
    }
    /* a message takes the hint's place until the next key: both don't fit */
    if (status_msg[0]) fui_text(LX0 + 4, SY0 + 4, status_msg, U_AMBER);
    else fui_text(LX0 + 4, SY0 + 4, h, U_TEXT);
}

static void draw(void) {
    const node_t *cur = stack[depth];
    const node_t *rows[MAX_ROWS];
    const int c = rows_of(cur, rows);
    int *sel = &sel_stack[depth], *top = &top_stack[depth];
    if (*sel >= c) *sel = c - 1;
    if (*sel < *top) *top = *sel;
    if (*sel >= *top + LIST_ROWS) *top = *sel - LIST_ROWS + 1;

    window(WX0, WY0, WX1, WY1, "IDTECH1 SETTINGS", G->game);

    /* breadcrumb: the levels above dim */
    int x = LX0;
    for (int i = 0; i <= depth; i++) {
        if (i) x = fui_text(x, CRUMB_Y, " > ", U_DIM);
        x = fui_text(x, CRUMB_Y, stack[i]->label, i == depth ? U_TEXT : U_DIM);
    }

    well(LX0, LY0, LX1, LY1, U_WELL);
    char buf[16];
    for (int r = 0; r < LIST_ROWS && *top + r < c; r++) {
        const int i = *top + r;
        const int y = LY0 + 2 + r * ROW_H;
        const bool s = i == *sel;
        if (s) fui_fill(LX0 + 2, y, LX1 - LX0 - 4, ROW_H, capturing ? U_SEL_UNF : U_SEL);
        fui_text_n(LX0 + 5, y + 2, rows[i]->label, 13, s ? U_WHITE : U_TEXT);
        value_text(rows[i], buf, sizeof(buf));
        fui_text_right(LX1 - 6, y + 2, buf, s ? U_WHITE : U_AMBER);
    }
    /* more rows than fit: a scroll bar along the list's right edge */
    if (c > LIST_ROWS) {
        const int h = LY1 - LY0 - 4;
        const int th = h * LIST_ROWS / c, ty = LY0 + 2 + (h - th) * *top / (c - LIST_ROWS);
        fui_fill(LX1 - 3, LY0 + 2, 1, h, U_DK);
        fui_fill(LX1 - 3, ty, 1, th, U_AMBER);
    }
    draw_right(rows[*sel]);
    draw_status(rows[*sel]);
}

/* ---- EasyRequest ---- */

static bool confirm(const char *title, const char *text) {
    const int w = 220, h = 66;
    const int x0 = (FUI_W - w) / 2, y0 = (FUI_H - h) / 2;
    char t[40];
    snprintf(t, sizeof(t), "IDTECH1 Request: %s", title);
    bool yes = false;
    for (;;) {
        window(x0, y0, x0 + w, y0 + h, t, NULL);
        fui_text_center(x0, x0 + w, y0 + 22, text, U_TEXT);
        button(x0 + 44, y0 + h - 23, "Yes", yes);
        button(x0 + w - 44, y0 + h - 23, "No", !yes);
        fui_present();
        const int k = fui_key(-1);
        if (k == KEY_LEFTARROW || k == KEY_RIGHTARROW || k == KEY_TAB) yes = !yes;
        else if (k == 'y') return true;
        else if (k == 'n' || k == KEY_ESCAPE) break;
        else if (k == KEY_ENTER) return yes;
    }
    return false;
}

/* ---- the session ---- */

static void adjust(const node_t *n, int dir) {
    const int v = node_get(n);
    if (n->kind == K_INT) {
        const int nv = clampi(v + dir, n->lo, n->hi);
        if (nv != v) node_set(n, nv);
    } else if (n->kind == K_BOOL) {
        node_set(n, !v);
    } else if (n->kind == K_RADIO) {
        const int span = n->hi - n->lo + 1;
        node_set(n, n->lo + ((v - n->lo + dir) % span + span) % span);
    }
}

/* a new key for n; whoever had it gets n's old key */
static void bind_key(const node_t *n, int key) {
    const int old = *n->var;
    for (int p = 0; p < COUNT(key_pages); p++)
        for (int i = 0; i < key_pages[p].count; i++) {
            const node_t *o = &key_pages[p].n[i];
            if (o != n && (!o->visible || o->visible()) && *o->var == key) *o->var = old;
        }
    *n->var = key;
}

/* returns false when the window should close */
static bool handle_key(int k) {
    const node_t *cur = stack[depth];
    const node_t *rows[MAX_ROWS];
    const int c = rows_of(cur, rows);
    int *sel = &sel_stack[depth];
    const node_t *n = rows[*sel];
    status_msg[0] = 0;

    if (capturing) {
        /* Esc cancels; F12 stays the settings key */
        if (k != KEY_ESCAPE && k != KEY_F12) {
            bind_key(capturing, k);
            snprintf(status_msg, sizeof(status_msg), "%s: %s", capturing->label, key_name(k));
        }
        capturing = NULL;
        return true;
    }

    switch (k) {
    case KEY_F12:
        return false;
    case KEY_ESCAPE:
    case KEY_BACKSPACE:
        if (depth == 0) return false;
        depth--;
        break;
    case KEY_UPARROW: *sel = (*sel + c - 1) % c; break;
    case KEY_DOWNARROW: *sel = (*sel + 1) % c; break;
    case KEY_PGUP: *sel = *sel > LIST_ROWS ? *sel - LIST_ROWS : 0; break;
    case KEY_PGDN: *sel = *sel + LIST_ROWS < c ? *sel + LIST_ROWS : c - 1; break;
    case KEY_HOME: *sel = 0; break;
    case KEY_END: *sel = c - 1; break;
    case KEY_LEFTARROW:
        if (n->kind == K_INT || n->kind == K_BOOL || n->kind == K_RADIO) adjust(n, -1);
        else if (depth > 0) depth--;
        break;
    case KEY_RIGHTARROW:
    case KEY_ENTER:
        if (n->kind == K_SUB && depth + 1 < MAX_DEPTH) {
            stack[++depth] = n;
            sel_stack[depth] = top_stack[depth] = 0;
        } else if (n->kind == K_ACTION) {
            if (k != KEY_RIGHTARROW) n->run();
        } else if (n->kind == K_KEY) {
            if (k == KEY_ENTER) capturing = n;
        } else if (n->kind != K_INT || k == KEY_RIGHTARROW) {
            adjust(n, +1);
        }
        break;
    default:
        /* a letter jumps to the next row starting with it */
        if (k >= 'a' && k <= 'z') {
            for (int i = 1; i <= c; i++) {
                const int j = (*sel + i) % c;
                char f = rows[j]->label[0];
                if (f >= 'A' && f <= 'Z') f = (char)(f - 'A' + 'a');
                if (f == k) { *sel = j; break; }
            }
        }
        break;
    }
    return !quit_now;
}

/* the game's colours, for the picture around the window */
static uint32_t game_rgb(int i) {
    return (uint32_t)colors[i].r << 16 | (uint32_t)colors[i].g << 8 | colors[i].b;
}

static void remap_backdrop(void) {
    uint8_t map[16];
    for (int u = 0; u < 16; u++) {
        const uint32_t c = game_rgb(224 + u);
        int best = 0, bd = 1 << 30;
        for (int j = 0; j < 224; j++) {
            const uint32_t d = game_rgb(j);
            const int dr = (int)(c >> 16) - (int)(d >> 16);
            const int dg = (int)((c >> 8) & 255) - (int)((d >> 8) & 255);
            const int db = (int)(c & 255) - (int)(d & 255);
            const int dist = dr * dr + dg * dg + db * db;
            if (dist < bd) { bd = dist; best = j; }
        }
        map[u] = (uint8_t)best;
    }
    uint8_t *fb = fui_back();
    for (int i = 0; i < FUI_W * FUI_H; i++)
        if (fb[i] >= 224 && fb[i] < 240) fb[i] = map[fb[i] - 224];
}

void fui_settings_run(void) {
    G = fs_glue_active();
    if (!G) return;
    quit_now = false;
    status_msg[0] = 0;
    depth = 0;
    stack[0] = &root;
    sel_stack[0] = top_stack[0] = 0;
    capturing = NULL;

    fui_sync_back();          /* the last game frame */
    remap_backdrop();
    for (int i = 0; i < 16; i++) fui_pal((uint8_t)(224 + i), ui_rgb[i]);
    fui_dirty_all();
    fui_key_flush();

    for (;;) {
        if (palette_changed) {  /* gamma: the picture around the window */
            for (int i = 0; i < 224; i++) fui_pal((uint8_t)i, game_rgb(i));
            palette_changed = false;
        }
        draw();
        fui_present();
        if (!handle_key(fui_key(-1))) break;
    }

    if (quit_now) {
        I_Quit();  /* saves the config, reboots to the WAD selector */
    }

    /* back to the game: its last frame and its whole palette, in one flip */
    fui_wait_back();
    memcpy(fui_back(), I_VideoBuffer, FUI_W * FUI_H);
    for (int i = 0; i < 256; i++) fui_pal((uint8_t)i, game_rgb(i));
    palette_changed = false;
    fui_game_frame(true);
    fui_key_flush();
}
