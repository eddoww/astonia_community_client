/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * FFXIV-style gamepad / controller support.
 *
 * All gamepad state is private to gamepad.c.  Call gamepad_init() once after
 * SDL is up, gamepad_shutdown() on exit, and gamepad_tick() every frame.
 * The SDL event loop in sdl_core.c routes the five gamepad event types here.
 */

#ifndef GAMEPAD_H
#define GAMEPAD_H

#include <SDL3/SDL.h>

void gamepad_init(void);
void gamepad_shutdown(void);
void gamepad_tick(void);
int gamepad_is_connected(void);

void gamepad_on_added(SDL_JoystickID id);
void gamepad_on_removed(SDL_JoystickID id);
void gamepad_button_down(SDL_GamepadButton button);
void gamepad_button_up(SDL_GamepadButton button);
void gamepad_axis_motion(SDL_GamepadAxis axis, int16_t value);

#endif /* GAMEPAD_H */
