// Game dispatch for multi-game binary
// Provides unprefixed symbols that the common dehacked parser needs.
// These pointers are set by game_dispatch_init() before D_DoomMain runs.

#include "deh_defs.h"

// Game selection from doomgeneric.c: 0 = doom, 1 = heretic
extern int active_game;

// Prefixed versions from each game engine (defined in game-specific code)
extern deh_section_t *DOOM_deh_section_types[];
extern const char *DOOM_deh_signatures[];
extern deh_section_t *HERETIC_deh_section_types[];
extern const char *HERETIC_deh_signatures[];

// The common deh_main.c expects these as extern arrays:
//   extern deh_section_t *deh_section_types[];
//   extern const char *deh_signatures[];
//
// We define them as pointer variables (which are compatible with array
// extern declarations for indexing purposes in C), initialized to the
// Heretic versions by default. game_dispatch_init() updates them based
// on the selected game.

// Note: C allows "extern T x[]" to be satisfied by a definition "T *x"
// only if the code uses x[i] (which decays to *(x+i)). However, the
// extern declarations in deh_defs.h declare these as arrays, and in C,
// an array declaration and pointer definition are NOT compatible at the
// object level - the linker would see different-sized symbols.
//
// Instead, we define actual arrays. We initialize them with a NULL
// sentinel and use game_dispatch_init() to copy the correct game's
// array into these. This requires knowing the max size.

// Maximum number of DEH section types any game can have
#define MAX_DEH_SECTIONS 16
#define MAX_DEH_SIGNATURES 8

// Actual arrays that the common code references
deh_section_t *deh_section_types[MAX_DEH_SECTIONS] = { NULL };
const char *deh_signatures[MAX_DEH_SIGNATURES] = { NULL };

// Called before D_DoomMain to set up the correct DEH tables
void game_dispatch_init(void)
{
    deh_section_t **src_sections;
    const char **src_sigs;
    int i;

    if (active_game == 0) {
        src_sections = DOOM_deh_section_types;
        src_sigs = DOOM_deh_signatures;
    } else {
        src_sections = HERETIC_deh_section_types;
        src_sigs = HERETIC_deh_signatures;
    }

    // Copy section types
    for (i = 0; i < MAX_DEH_SECTIONS - 1 && src_sections[i] != NULL; i++) {
        deh_section_types[i] = src_sections[i];
    }
    deh_section_types[i] = NULL;

    // Copy signatures
    for (i = 0; i < MAX_DEH_SIGNATURES - 1 && src_sigs[i] != NULL; i++) {
        deh_signatures[i] = src_sigs[i];
    }
    deh_signatures[i] = NULL;
}
