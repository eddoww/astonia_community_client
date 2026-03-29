/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 */

#include <math.h>
#include <SDL3/SDL.h>
#include "astonia.h"
#include "sdl/gamepad.h"
#include "gui/gui.h"
#include "gui/input_bind.h"
#include "sdl/sdl.h"

extern SDL_Window *sdlwnd;
extern int mousex, mousey;

static SDL_Gamepad *pad = NULL;
static SDL_JoystickID pad_id = 0;

static int lt_held = 0;
static int rt_held = 0;

static int16_t left_x = 0;
static int16_t left_y = 0;
static int16_t right_x = 0;
static int16_t right_y = 0;

#define STICK_DEADZONE    8000
#define TRIGGER_THRESHOLD 8000
#define MOVE_REPEAT_MS    167u

static Uint64 move_last_ms = 0;

static void pad_hotbar_press(SDL_GamepadButton button);

void gamepad_init(void)
{
	int count = 0;

	SDL_SetGamepadEventsEnabled(true);

	SDL_JoystickID *ids = SDL_GetGamepads(&count);
	if (ids) {
		if (count > 0) {
			gamepad_on_added(ids[0]);
		}
		SDL_free(ids);
	}
}

void gamepad_shutdown(void)
{
	if (pad) {
		SDL_CloseGamepad(pad);
		pad = NULL;
	}
}

int gamepad_is_connected(void)
{
	return pad != NULL;
}

void gamepad_on_added(SDL_JoystickID id)
{
	if (pad) {
		return;
	}
	pad = SDL_OpenGamepad(id);
	if (!pad) {
		return;
	}
	pad_id = id;
	addline("Gamepad connected: %s", SDL_GetGamepadName(pad));
}

void gamepad_on_removed(SDL_JoystickID id)
{
	int count = 0;

	if (id != pad_id) {
		return;
	}
	if (pad) {
		SDL_CloseGamepad(pad);
		pad = NULL;
	}
	addline("Gamepad disconnected");

	SDL_JoystickID *ids = SDL_GetGamepads(&count);
	if (ids) {
		if (count > 0) {
			gamepad_on_added(ids[0]);
		}
		SDL_free(ids);
	}
}

void gamepad_button_down(SDL_GamepadButton button)
{
	InputBinding *b;

	switch (button) {
	case SDL_GAMEPAD_BUTTON_START:
		b = input_find_by_id("ui.menu");
		if (b && b->on_press) {
			b->on_press(b);
		}
		break;

	case SDL_GAMEPAD_BUTTON_BACK:
		b = input_find_by_id("ui.cancel");
		if (b && b->on_press) {
			b->on_press(b);
		}
		break;

	case SDL_GAMEPAD_BUTTON_LEFT_STICK:
		gui_sdl_mouseproc((float)mousex, (float)mousey, SDL_MOUM_RDOWN);
		break;

	case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
		gui_sdl_mouseproc((float)mousex, (float)mousey, SDL_MOUM_LDOWN);
		break;

	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
		break;

	case SDL_GAMEPAD_BUTTON_SOUTH:
	case SDL_GAMEPAD_BUTTON_EAST:
	case SDL_GAMEPAD_BUTTON_WEST:
	case SDL_GAMEPAD_BUTTON_NORTH:
	case SDL_GAMEPAD_BUTTON_DPAD_UP:
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
		pad_hotbar_press(button);
		break;

	default:
		break;
	}
}

void gamepad_button_up(SDL_GamepadButton button)
{
	switch (button) {
	case SDL_GAMEPAD_BUTTON_LEFT_STICK:
		gui_sdl_mouseproc((float)mousex, (float)mousey, SDL_MOUM_RUP);
		break;
	case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
		gui_sdl_mouseproc((float)mousex, (float)mousey, SDL_MOUM_LUP);
		break;
	default:
		break;
	}
}

void gamepad_axis_motion(SDL_GamepadAxis axis, int16_t value)
{
	switch (axis) {
	case SDL_GAMEPAD_AXIS_LEFTX:
		left_x = value;
		break;
	case SDL_GAMEPAD_AXIS_LEFTY:
		left_y = value;
		break;
	case SDL_GAMEPAD_AXIS_RIGHTX:
		right_x = value;
		break;
	case SDL_GAMEPAD_AXIS_RIGHTY:
		right_y = value;
		break;
	case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
		lt_held = (value > (int16_t)TRIGGER_THRESHOLD) ? 1 : 0;
		break;
	case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
		rt_held = (value > (int16_t)TRIGGER_THRESHOLD) ? 1 : 0;
		break;
	default:
		break;
	}
}

static void pad_hotbar_press(SDL_GamepadButton button)
{
	int offset = -1;
	int slot = -1;

	switch (button) {
	case SDL_GAMEPAD_BUTTON_SOUTH:
		offset = 0;
		break;
	case SDL_GAMEPAD_BUTTON_EAST:
		offset = 1;
		break;
	case SDL_GAMEPAD_BUTTON_WEST:
		offset = 2;
		break;
	case SDL_GAMEPAD_BUTTON_NORTH:
		offset = 3;
		break;
	case SDL_GAMEPAD_BUTTON_DPAD_UP:
		offset = 4;
		break;
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
		offset = 5;
		break;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
		offset = 6;
		break;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
		offset = 7;
		break;
	default:
		return;
	}

	if (lt_held && !rt_held) {
		slot = offset;
	} else if (rt_held && !lt_held) {
		slot = 8 + offset;
	} else if (lt_held && rt_held) {
		slot = 16 + offset;
	}

	if (slot >= 0 && slot < HOTBAR_MAX_SLOTS) {
		hotbar_activate(slot);
	}
}

void gamepad_tick(void)
{
	Uint64 now;
	int dx, dy;
	int ax, ay;
	float rx, ry, mag, deadzone_norm, adjusted;
	int nx, ny;

	if (!pad) {
		return;
	}

	now = SDL_GetTicks();

	if (now >= move_last_ms + MOVE_REPEAT_MS) {
		dx = 0;
		dy = 0;
		if ((int)left_x < -STICK_DEADZONE) {
			dx = -1;
		}
		if ((int)left_x > STICK_DEADZONE) {
			dx = 1;
		}
		if ((int)left_y < -STICK_DEADZONE) {
			dy = -1;
		}
		if ((int)left_y > STICK_DEADZONE) {
			dy = 1;
		}

		if (dx || dy) {
			if (dy < 0) {
				keyboard_move_press(KMOVE_UP);
			}
			if (dy > 0) {
				keyboard_move_press(KMOVE_DOWN);
			}
			if (dx < 0) {
				keyboard_move_press(KMOVE_LEFT);
			}
			if (dx > 0) {
				keyboard_move_press(KMOVE_RIGHT);
			}
			move_last_ms = now;
		}
	}

	if ((int)left_x > -STICK_DEADZONE && (int)left_x < STICK_DEADZONE && (int)left_y > -STICK_DEADZONE &&
	    (int)left_y < STICK_DEADZONE) {
		keyboard_move_release(KMOVE_UP);
		keyboard_move_release(KMOVE_DOWN);
		keyboard_move_release(KMOVE_LEFT);
		keyboard_move_release(KMOVE_RIGHT);
	}

	ax = abs((int)right_x);
	ay = abs((int)right_y);

	if (ax > STICK_DEADZONE || ay > STICK_DEADZONE) {
		rx = (float)right_x / 32767.0f;
		ry = (float)right_y / 32767.0f;
		mag = sqrtf(rx * rx + ry * ry);
		if (mag > 0.001f) {
			deadzone_norm = (float)STICK_DEADZONE / 32767.0f;
			adjusted = (mag - deadzone_norm) / (1.0f - deadzone_norm);
			if (adjusted < 0.0f) {
				adjusted = 0.0f;
			}
			rx = (rx / mag) * adjusted * 4.0f;
			ry = (ry / mag) * adjusted * 4.0f;
		}
		nx = mousex + (int)rx;
		ny = mousey + (int)ry;
		if (nx < 0) {
			nx = 0;
		}
		if (ny < 0) {
			ny = 0;
		}
		if (nx >= XRES * sdl_scale) {
			nx = XRES * sdl_scale - 1;
		}
		if (ny >= YRES * sdl_scale) {
			ny = YRES * sdl_scale - 1;
		}
		SDL_WarpMouseInWindow(sdlwnd, (float)nx, (float)ny);
	}
}
