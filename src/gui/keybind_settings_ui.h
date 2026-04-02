/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Keybind Settings UI — full-screen overlay panel listing all keybindings
 * grouped by category, with rebind and reset per binding.
 * Opened via ESC key (when not in chat).
 */

#ifndef KEYBIND_SETTINGS_UI_H
#define KEYBIND_SETTINGS_UI_H

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_stdinc.h>

void keybind_settings_display(void);
int keybind_settings_click(int mx, int my);
int keybind_settings_scroll(int delta);
int keybind_settings_is_open(void);
void keybind_settings_toggle(void);
void keybind_settings_close(void);
int keybind_settings_capturing(void);
void keybind_settings_accept_key(SDL_Keycode key, Uint8 mods);
void keybind_settings_cancel_capture(void);

#endif /* KEYBIND_SETTINGS_UI_H */
