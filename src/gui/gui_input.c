/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Graphical User Interface - Input handling (keyboard and mouse)
 *
 */

#include <inttypes.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "gui/panels.h"
#include "gui/spellbook_ui.h"
#include "gui/keybind_ui.h"
#include "gui/keybind_settings_ui.h"
#include "gui/escape_menu_ui.h"
#include "gui/options_ui.h"
#include "client/client.h"
#include "game/game.h"
#include "sdl/sdl.h"
#include "modder/modder.h"

/* set on right-button-down when the click cancelled target selection, so the
 * matching button-up doesn't execute the look command on top of the cancel */
static int rclick_cancelled_targeting;

void gui_sdl_keyproc(SDL_Keycode key)
{
	if (keybind_settings_capturing()) {
		if (key == SDLK_ESCAPE) {
			keybind_settings_cancel_capture();
			return;
		}
		if (key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LCTRL || key == SDLK_RCTRL || key == SDLK_LALT ||
		    key == SDLK_RALT) {
			return;
		}
		keybind_settings_accept_key(key, input_current_modifiers());
		sdl_flush_textinput();
		return;
	}

	/* keybind panel key capture — intercept before anything else.
	 * ignore modifier-only keys so Shift+E doesn't bind to "Shift". */
	if (keybind_panel_capturing()) {
		if (key == SDLK_ESCAPE) {
			keybind_panel_cancel_capture();
			return;
		}
		if (key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LCTRL || key == SDLK_RCTRL || key == SDLK_LALT ||
		    key == SDLK_RALT) {
			return; /* wait for a real key */
		}
		keybind_panel_accept_key(key, input_current_modifiers());
		sdl_flush_textinput();
		return;
	}

	/* While a loading screen is up the game GUI is not there yet: no mod
	 * keys, no bindings, no chat. Escape still works so the player can
	 * reach the menu (options, exit game). */
	if (gui_is_loading()) {
		if (key == SDLK_ESCAPE) {
			if (keybind_settings_is_open()) {
				keybind_settings_close();
			} else if (options_is_open()) {
				options_close();
			} else {
				escape_menu_toggle();
			}
		}
		return;
	}

	/* let mods intercept first (except ESC and F12 which are non-rebindable) */
	if (key != SDLK_ESCAPE && key != SDLK_F12 && amod_keydown(key)) {
		return;
	}

	/* text editing keys — always go to the command line, never through bindings */
	switch (key) {
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		cmd_proc(CMD_RETURN);
		return;
	case SDLK_DELETE:
		cmd_proc(CMD_DELETE);
		return;
	case SDLK_BACKSPACE:
		cmd_proc(CMD_BACK);
		return;
	case SDLK_LEFT:
		if (cmd_is_active()) {
			cmd_proc(CMD_LEFT);
			return;
		}
		keyboard_move_press(KMOVE_LEFT);
		return;
	case SDLK_RIGHT:
		if (cmd_is_active()) {
			cmd_proc(CMD_RIGHT);
			return;
		}
		keyboard_move_press(KMOVE_RIGHT);
		return;
	case SDLK_HOME:
		cmd_proc(CMD_HOME);
		return;
	case SDLK_END:
		cmd_proc(CMD_END);
		return;
	case SDLK_UP:
		if (cmd_is_active()) {
			cmd_proc(CMD_UP);
			return;
		}
		keyboard_move_press(KMOVE_UP);
		return;
	case SDLK_DOWN:
		if (cmd_is_active()) {
			cmd_proc(CMD_DOWN);
			return;
		}
		keyboard_move_press(KMOVE_DOWN);
		return;
	case SDLK_TAB:
		cmd_proc(9);
		return;
	case SDLK_INSERT:
		if (vk_shift && !vk_control && !vk_alt) {
			gui_insert();
		}
		return;
	default:
		break;
	}

	/* if chat is active, all keys go to chat - no hotkeys fire */
	if (cmd_is_active()) {
		return;
	}

	/* build modifier mask for binding lookup */
	Uint8 mods = input_current_modifiers();

	/* check hotbar extra binds first (modifier combos take priority) */
	{
		int hb_slot = hotbar_find_extra_bind(key, mods);
		if (hb_slot >= 0) {
			hotbar_activate_extra(hb_slot, key, mods);
			sdl_flush_textinput();
			return;
		}
	}

	/* try the unified binding system */
	InputBinding *b = input_find(key, mods);
	if (b) {
		input_execute(b);
		sdl_flush_textinput();
		return;
	}

	/* Shift+hotbar key → quick-cast: strip Shift and retry.
	 * Only triggers when Shift is the sole modifier and the underlying
	 * binding is a hotbar slot — other bindings are not affected. */
	if ((mods & INPUT_MOD_SHIFT) && !(mods & ~INPUT_MOD_SHIFT)) {
		b = input_find(key, INPUT_MOD_NONE);
		if (b && b->category == INPUT_CAT_HOTBAR) {
			hotbar_activate_with_mode(b->param, CAST_QUICK);
			sdl_flush_textinput();
			return;
		}
	}

	/* no modifiers held: letter/number keys go to the action bar context system */
	if (!vk_item && !vk_char && !vk_spell) {
		context_keydown(key);
	}
}

void gui_sdl_mouseproc(float x, float y, int what)
{
	int delta, tmp;
	static int mdown = 0;

	// SDL3 provides sub-pixel mouse precision via floats, but we work with discrete UI elements
	// and tile-based positioning, so we convert to int immediately. Precision loss is negligible
	// for user interaction in this context.
	int local_x = (int)x;
	int local_y = (int)y;

	switch (what) {
	case SDL_MOUM_NONE:
		mousex = local_x;
		mousey = local_y;

		if (capbut != -1) {
			/* canvas centre in window coordinates - mousex/mousey are
			 * still raw window coords at this point */
			int cwx = (XRES / 2 + render_offset_x()) * sdl_scale;
			int cwy = (YRES / 2 + render_offset_y()) * sdl_scale;
			if (mousex != cwx || mousey != cwy) {
				/* carry the sub-scale remainder between events - integer
				 * division alone throws away 1-2 px deltas entirely at
				 * sdl_scale>=2, which made slow scrollbar drags register
				 * nothing and then jump */
				static int capdx_rem, capdy_rem;
				int rawx = mousex - cwx + capdx_rem;
				int rawy = mousey - cwy + capdy_rem;
				mousedx += rawx / sdl_scale;
				mousedy += rawy / sdl_scale;
				capdx_rem = rawx % sdl_scale;
				capdy_rem = rawy % sdl_scale;
				sdl_set_cursor_pos(cwx, cwy);
			}
		}

		mousex /= sdl_scale;
		mousey /= sdl_scale;
		mousex -= render_offset_x();
		mousey -= render_offset_y();

		if (gui_is_loading()) {
			break; /* hover only feeds the menu overlays while loading */
		}

		if (butsel != -1 && vk_lbut && (but[butsel].flags & BUTF_MOVEEXEC)) {
			exec_cmd(lcmd, 0);
		}

		amod_mouse_move(mousex, mousey);
		break;

	case SDL_MOUM_LDOWN:
		vk_lbut = 1;

		if (gui_is_loading()) {
			break; /* clicks are handled on release while loading */
		}

		if (amod_mouse_click(mousex, mousey, what)) {
			break;
		}

		if (butsel >= BUT_HOTBAR_BEG && butsel <= BUT_HOTBAR_END) {
			hotbar_mousedown(butsel - BUT_HOTBAR_BEG);
		}

		if (butsel != -1 && capbut == -1 && (but[butsel].flags & BUTF_CAPTURE)) {
			amod_mouse_capture(1);
			SDL_HideCursor();
			sdl_capture_mouse(1);
			mousedx = 0;
			mousedy = 0;
			sdl_set_cursor_pos((XRES / 2 + render_offset_x()) * sdl_scale, (YRES / 2 + render_offset_y()) * sdl_scale);
			capbut = butsel;
		}
		break;


	case SDL_MOUM_MUP:
		shift_override = 0;
		control_override = 0;
		mdown = 0;
		if ((game_options & GO_WHEEL) && special_tab[vk_special].spell) {
			if (special_tab[vk_special].target == TGT_MAP) {
				exec_cmd(CMD_MAP_CAST_K, special_tab[vk_special].spell);
			} else if (special_tab[vk_special].target == TGT_CHR) {
				exec_cmd(CMD_CHR_CAST_K, special_tab[vk_special].spell);
			} else if (special_tab[vk_special].target == TGT_SLF) {
				exec_cmd(CMD_SLF_CAST_K, special_tab[vk_special].spell);
			}
			break;
		}
		// fall through intended
	case SDL_MOUM_LUP:
		vk_lbut = 0;

		/* loading screen: only the escape menu and its windows are live */
		if (gui_is_loading()) {
			if (options_is_open()) {
				/* clicks outside are swallowed, not a close - see below */
				options_click(mousex, mousey);
			} else if (escape_menu_is_open()) {
				if (!escape_menu_click(mousex, mousey)) {
					escape_menu_close();
				}
			} else if (keybind_settings_is_open()) {
				keybind_settings_click(mousex, mousey);
			}
			break;
		}

		if (amod_mouse_click(mousex, mousey, what)) {
			break;
		}
		if (context_click(mousex, mousey)) {
			break;
		}

		/* Options and Keybindings swallow outside clicks instead of closing:
		 * clicking the chat to answer someone used to dismiss the window and
		 * lose the player's place. ESC and the X button close them; the
		 * escape MENU below stays click-away-dismissable like any popup. */
		if (options_is_open()) {
			options_click(mousex, mousey);
			break;
		}

		if (escape_menu_is_open()) {
			if (escape_menu_click(mousex, mousey)) {
				break;
			}
			escape_menu_close();
			break;
		}

		if (keybind_settings_is_open()) {
			keybind_settings_click(mousex, mousey);
			break;
		}

		/* keybind panel — consume clicks inside, close on click outside */
		if (keybind_panel_is_open()) {
			if (keybind_panel_click(mousex, mousey)) {
				break;
			}
			keybind_panel_close();
		}

		/* tutorial popup close button */
		if (tutor_click(mousex, mousey)) {
			break;
		}

		/* spellbook toggle button */
		if (panel_shown(PANEL_HOTBAR) && hotbar_toggle_hit(mousex, mousey)) {
			break;
		}

		/* hotbar: assign (drag) or activate (click) */
		if (butsel >= BUT_HOTBAR_BEG && butsel <= BUT_HOTBAR_END) {
			if (hotbar_click(butsel - BUT_HOTBAR_BEG)) {
				break;
			}
		}

		if (hotbar_is_dragging()) {
			hotbar_cancel_drag();
			break;
		}

		/* spellbook panel clicks (pick up spell, or cancel drag) */
		if (panel_shown(PANEL_HOTBAR) && spellbook_click(mousex, mousey)) {
			break;
		}

		if (capbut != -1) {
			sdl_set_cursor_pos(
			    (but[capbut].x + render_offset_x()) * sdl_scale, (but[capbut].y + render_offset_y()) * sdl_scale);
			sdl_capture_mouse(0);
			SDL_ShowCursor();
			amod_mouse_capture(0);
			if (!(but[capbut].flags & BUTF_MOVEEXEC)) {
				exec_cmd(lcmd, 0);
			}
			if (capbut >= BUT_DRAG_BEG && capbut <= BUT_DRAG_END) {
				panels_drag_finished(); /* persist the moved layout */
			}
			capbut = -1;
		} else {
			if ((tmp = context_key_click()) != CMD_NONE) {
				exec_cmd(tmp, 0);
			} else {
				exec_cmd(lcmd, 0);
			}
		}
		break;

	case SDL_MOUM_RDOWN:
		vk_rbut = 1;
		if (gui_is_loading()) {
			break;
		}
		if (amod_mouse_click(mousex, mousey, what)) {
			break;
		}
		/* right-click during target selection only cancels the cast - it
		 * must not fall through to the look command, which popped the
		 * character description window over the cancel */
		if (context_targeting_active()) {
			context_key_reset();
			action_ovr = ACTION_NONE;
			rclick_cancelled_targeting = 1;
			hotbar_cancel_held();
			hotbar_cancel_drag();
			context_stop();
			break;
		}
		hotbar_cancel_held();
		hotbar_cancel_drag();
		context_stop();
		break;

	case SDL_MOUM_RUP:
		vk_rbut = 0;
		if (gui_is_loading()) {
			break;
		}
		/* swallow the release of a right-click that cancelled targeting */
		if (rclick_cancelled_targeting) {
			rclick_cancelled_targeting = 0;
			break;
		}
		if (amod_mouse_click(mousex, mousey, what)) {
			break;
		}
		/* right-click cancels spellbook drag */
		if (spellbook_rclick(mousex, mousey)) {
			break;
		}
		/* keybind panel right-click (cycle backward on cast/target) */
		if (keybind_panel_is_open()) {
			if (keybind_panel_rclick(mousex, mousey)) {
				break;
			}
			keybind_panel_close();
			break;
		}
		/* right-click hotbar slot opens keybind config panel */
		if (butsel >= BUT_HOTBAR_BEG && butsel <= BUT_HOTBAR_END) {
			keybind_panel_open(butsel - BUT_HOTBAR_BEG);
			break;
		}
		if (rcmd == CMD_MAP_LOOK && context_open(mousex, mousey)) {
			break;
		}
		context_stop();
		exec_cmd(rcmd, 0);
		break;

	case SDL_MOUM_WHEEL:
		delta = local_y;

		if (keybind_settings_is_open()) {
			keybind_settings_scroll(delta > 0 ? -1 : 1);
			break;
		}

		if (options_is_open()) {
			options_scroll(delta > 0 ? -1 : 1);
			break;
		}

		if (gui_is_loading()) {
			break;
		}

		if (amod_mouse_click(0, delta, what)) {
			break;
		}

		/* big-map zoom - the big map draws on top of the side panels
		 * (display_minimap runs after them), so it gets the wheel first */
		if (minimap_wheel_zoom(mousex, mousey, delta)) {
			break;
		}

		if (mousex >= dotx(DOT_SKL) && mousex < dotx(DOT_SK2) && mousey >= doty(DOT_SKL) &&
		    mousey < doty(DOT_SK2)) { // skill / depot / merchant
			while (delta > 0) {
				if (!con_cnt) {
					set_skloff(0, skloff - 1);
				} else {
					set_conoff(0, conoff - 1);
				}
				delta--;
			}
			while (delta < 0) {
				if (!con_cnt) {
					set_skloff(0, skloff + 1);
				} else {
					set_conoff(0, conoff + 1);
				}
				delta++;
			}
			break;
		}

		if (mousex >= dotx(DOT_TXT) && mousex < dotx(DOT_TX2) && mousey >= doty(DOT_TXT) &&
		    mousey < doty(DOT_TX2)) { // chat
			while (delta > 0) {
				render_text_lineup();
				render_text_lineup();
				render_text_lineup();
				delta--;
			}
			while (delta < 0) {
				render_text_linedown();
				render_text_linedown();
				render_text_linedown();
				delta++;
			}
			break;
		}

		if (mousex >= dotx(DOT_IN1) && mousex < dotx(DOT_IN2) && mousey >= doty(DOT_IN1) &&
		    mousey < doty(DOT_IN2)) { // inventory
			while (delta > 0) {
				set_invoff(0, invoff - 1);
				delta--;
			}
			while (delta < 0) {
				set_invoff(0, invoff + 1);
				delta++;
			}
			break;
		}

		if (game_options & GO_WHEELSPEED) {
			// Mousewheel toggles movement speed mode
			// pspeed: 0=normal, 1=fast, 2=stealth
			// cmd_speed: 0=normal, 1=fast, 2=stealth
			while (delta > 0) {
				// Scroll up: cycle through normal->fast->stealth->normal
				if (pspeed == 0) {
					cmd_speed(1); // normal to fast
				} else if (pspeed == 1) {
					cmd_speed(2); // fast to stealth
				} else if (pspeed == 2) {
					cmd_speed(0); // stealth to normal
				}
				delta--;
			}
			while (delta < 0) {
				// Scroll down: cycle through normal->stealth->fast->normal
				if (pspeed == 0) {
					cmd_speed(2); // normal to stealth
				} else if (pspeed == 2) {
					cmd_speed(1); // stealth to fast
				} else if (pspeed == 1) {
					cmd_speed(0); // fast to normal
				}
				delta++;
			}
			break;
		}

		if (game_options & GO_WHEEL) {
			while (delta > 0) {
				vk_special_inc();
				delta--;
			}
			while (delta < 0) {
				vk_special_dec();
				delta++;
			}
			vk_special_time = now;

			if (mdown) {
				shift_override = special_tab[vk_special].shift_over;
				control_override = special_tab[vk_special].control_over;
			}
		}
		break;

	case SDL_MOUM_MDOWN:
		if (game_options & GO_WHEEL) {
			shift_override = special_tab[vk_special].shift_over;
			control_override = special_tab[vk_special].control_over;
		} else {
			shift_override = 1;
		}
		mdown = 1;
		break;

	case SDL_MOUM_X1DOWN:
	case SDL_MOUM_X2DOWN: {
		SDL_Keycode vk = (what == SDL_MOUM_X1DOWN) ? INPUT_MOUSE_X1 : INPUT_MOUSE_X2;

		if (gui_is_loading()) {
			break;
		}

		if (keybind_panel_capturing()) {
			keybind_panel_accept_key(vk, input_current_modifiers());
			break;
		}

		Uint8 mods = input_current_modifiers();

		int hb_slot = hotbar_find_extra_bind(vk, mods);
		if (hb_slot >= 0) {
			hotbar_activate_extra(hb_slot, vk, mods);
			break;
		}

		InputBinding *b = input_find(vk, mods);
		if (b) {
			input_execute(b);
		}
		break;
	}

	case SDL_MOUM_X1UP:
	case SDL_MOUM_X2UP: {
		SDL_Keycode vk = (what == SDL_MOUM_X1UP) ? INPUT_MOUSE_X1 : INPUT_MOUSE_X2;
		input_keyup(vk);
		break;
	}
	}
}
