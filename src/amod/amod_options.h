/*
 * Mod options exposed in the client's Options screen: any mod's rows appear
 * under its own name in the "Mods" tab; the system mod's rows are game
 * settings and are spread across the themed tabs by amod_option_tab().
 * Shared between the client (src/modder, src/gui) and mods (via amod_structs.h).
 * Keep this header self-contained: no other includes.
 */
#ifndef AMOD_OPTIONS_H
#define AMOD_OPTIONS_H

#define AMOD_OPT_HEADER 0 /* section header row (label only) */
#define AMOD_OPT_TOGGLE 1 /* checkbox: value 0/1 */
#define AMOD_OPT_SLIDER 2 /* slider: value in [min_val, max_val] */

/* Themed tab a *system mod* option row is shown in, reported per index by its
 * optional export `int amod_option_tab(int index)`; an unknown value gets
 * AMOD_TAB_GAMEPLAY, the historical behavior. Ordinary mods ignore this: their
 * rows always appear under their own name in the Mods tab. Kept out of struct
 * amod_option so the struct layout stays identical across client/mod version
 * mixes. */
#define AMOD_TAB_GAMEPLAY 0
#define AMOD_TAB_AUDIO    1
#define AMOD_TAB_UI       2

struct amod_option {
	int type; /* AMOD_OPT_* */
	int value; /* current value */
	int min_val; /* slider range (ignored for toggle/header) */
	int max_val;
	char label[48]; /* user-visible label */
};

#endif
