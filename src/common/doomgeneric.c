#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);

// Prefixed entry points from each game engine
extern void DOOM_D_DoomMain(void);
extern void DOOM_doomgeneric_Tick(void);
extern void HERETIC_D_DoomMain(void);
extern void HERETIC_doomgeneric_Tick(void);
extern void HEXEN_D_DoomMain(void);
extern void HEXEN_doomgeneric_Tick(void);
extern void STRIFE_D_DoomMain(void);
extern void STRIFE_doomgeneric_Tick(void);

// Initialize game dispatch tables (deh_section_types, etc.)
extern void game_dispatch_init(void);

// Game selection: 0 = doom, 1 = heretic, 2 = hexen, 3 = strife
int active_game = 1;  // default to heretic

void DG_StartScreen(void) __attribute__((weak));
void DG_StartScreen(void) {}

void doomgeneric_Create(int argc, char **argv)
{
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

	DG_Init();

	DG_StartScreen();

	// Set up game dispatch tables before calling D_DoomMain
	game_dispatch_init();

	// Dispatch to the correct game engine based on WAD selection
	if (active_game == 0) {
		DOOM_D_DoomMain();
	} else if (active_game == 2) {
		HEXEN_D_DoomMain();
	} else if (active_game == 3) {
		STRIFE_D_DoomMain();
	} else {
		HERETIC_D_DoomMain();
	}
}

void doomgeneric_Tick(void)
{
	if (active_game == 0) {
		DOOM_doomgeneric_Tick();
	} else if (active_game == 2) {
		HEXEN_doomgeneric_Tick();
	} else if (active_game == 3) {
		STRIFE_doomgeneric_Tick();
	} else {
		HERETIC_doomgeneric_Tick();
	}
}
