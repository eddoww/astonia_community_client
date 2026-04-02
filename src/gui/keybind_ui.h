/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Keybind Panel UI — floating panel for rebinding hotbar slot keys,
 * adding extra bindings with cast/target overrides.
 */

#ifndef KEYBIND_UI_H
#define KEYBIND_UI_H

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_stdinc.h>

/* rendering — call from gui_display after spellbook_display */
void keybind_panel_display(void);

/* open the panel for a specific hotbar slot */
void keybind_panel_open(int hotbar_slot);

/* close the panel */
void keybind_panel_close(void);

/* is the panel visible? */
int keybind_panel_is_open(void);

/* click handlers — return 1 if consumed */
int keybind_panel_click(int mx, int my);
int keybind_panel_rclick(int mx, int my);

/* key capture — is the panel waiting for a key press? */
int keybind_panel_capturing(void);

/* feed a captured key to the panel */
void keybind_panel_accept_key(SDL_Keycode key, Uint8 mods);

/* cancel key capture (ESC pressed) */
void keybind_panel_cancel_capture(void);

#endif /* KEYBIND_UI_H */
