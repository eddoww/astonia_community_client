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
#include "gui/gesture.h"
#include "gui/ui_tokens.h"

extern int ui_scale_pct; /* sdl_core.c */
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
static int mm_pan; /* a big-minimap pan gesture is live (either button) */

/* set on right-button-down when the click abandoned a live pointer gesture,
 * so the matching button-up does not execute the look command on top */
static int rclick_cancelled_gesture;

/* set when a gesture is abandoned (Escape, right-click) while the left
 * button is still held: the release that eventually comes belongs to the
 * dead gesture and must not turn into a click on whatever is under it */
static int lclick_release_pending;

/* The one pointer gesture. Whoever takes the left-button press - a client
 * button flagged BUTF_CAPTURE, or a mod that consumed the press - owns every
 * motion event and the matching release, whatever sits under the pointer
 * then. Positions are absolute: the pointer is never warped or hidden, so a
 * drag follows the hand 1:1 and can neither drift nor run away, and nothing
 * under the pointer can eat the release and leave the gesture stuck. */
static Gesture grab;

int gui_pointer_grabbed(void)
{
	return gesture_active(&grab);
}

/* Is a client overlay that butsel does not track sitting under (x,y)? The
 * modal windows (options, escape menu, keybinding editors) block the whole
 * screen while open, the rest are tested by their rectangles. Used before
 * an event that found no client control is offered to the mod's background
 * layer (the chat) - those surfaces draw UNDER all of this. */
int gui_client_overlay_at(int x, int y)
{
	if (options_is_open() || escape_menu_is_open() || keybind_settings_is_open() || keybind_panel_is_open()) {
		return 1;
	}
	if ((teleporter && !teleport_override) || show_color) {
		return 1;
	}
	if ((display_help || display_quest) && x >= dotx(DOT_HLP) - UI_WIN_PAD && x <= dotx(DOT_HL2) + UI_WIN_PAD &&
	    y >= doty(DOT_HLP) - UI_WIN_TITLE_H && y <= doty(DOT_HL2) + UI_WIN_PAD) {
		return 1;
	}
	if (show_tutor && x >= dotx(DOT_TUT) && x <= dotx(DOT_TUT) + 410 && y >= doty(DOT_TUT) &&
	    y <= doty(DOT_TUT) + 122) {
		return 1;
	}
	if (spellbook_over(x, y) || context_menu_is_open()) {
		return 1;
	}
	if (gui_overlay_visible && panels_frame_over(x, y)) {
		return 1;
	}
	return 0;
}

/* an event nothing of the client's claimed: may the mod's background layer
 * have it before it reaches the world? */
static int background_may_take(void)
{
	if (gui_client_overlay_at(mousex, mousey)) {
		return 0;
	}
	if (butsel == -1) {
		return 1;
	}
	/* the hit test tags a mod background surface as BUT_PANEL_BODY so the
	 * world under it is never targeted - that tag must not also keep the
	 * event from the surface itself (v1.8.0 shipped exactly that: the chat
	 * could neither be dragged, minimized nor scrolled) */
	return butsel == BUT_PANEL_BODY && !panels_frame_over(mousex, mousey) && amod_mouse_over_background(mousex, mousey);
}

/* a client-button gesture starts: the button keeps the pointer */
static void gesture_take_button(int b)
{
	gesture_begin(&grab, GESTURE_BUTTON, b, mousex, mousey);
	capbut = b;
	mousedx = mousedy = 0;
	/* motion and the release keep flowing while the pointer is outside the
	 * window: SDL auto-captures the mouse for as long as a button is held -
	 * nothing is warped, nothing is hidden, nothing is grabbed by hand */
	amod_mouse_capture(1);
	if (b >= BUT_DRAG_BEG && b <= BUT_DRAG_END) {
		panels_drag_begin(b - BUT_DRAG_BEG, mousex, mousey);
	} else if (b >= BUT_PSIZE_BEG && b <= BUT_PSIZE_END) {
		panels_resize_begin(b - BUT_PSIZE_BEG, mousex, mousey);
	}
}

/* the gesture is over: released (cancel=0) or abandoned (cancel=1 - the
 * panel goes back to where it was at the press) */
static void gesture_drop_button(int cancel)
{
	int b = grab.but;

	gesture_end(&grab);
	capbut = -1;
	mousedx = mousedy = 0;
	amod_mouse_capture(0);
	if (cancel && vk_lbut) {
		lclick_release_pending = 1;
	}
	if (b >= BUT_DRAG_BEG && b <= BUT_DRAG_END) {
		if (cancel) {
			panels_drag_cancel();
		}
		panels_drag_end(); /* persists the moved layout */
	} else if (b >= BUT_PSIZE_BEG && b <= BUT_PSIZE_END) {
		if (cancel) {
			panels_resize_cancel();
		}
		panels_resize_end();
	}
}

void gui_sdl_keyproc(SDL_Keycode key, SDL_Keymod mod)
{
	/* modifier state at the time the key event was generated — the live
	 * SDL_GetModState() may already reflect a later release when a quick
	 * modifier+key tap was fully pumped before this dispatch runs */
	Uint8 mods = input_mods_from_sdl(mod);

	/* Escape abandons a live panel drag/resize: the window returns to where
	 * it was at the press and nothing else happens on this keystroke */
	if (key == SDLK_ESCAPE && grab.kind == GESTURE_BUTTON) {
		gesture_drop_button(1);
		return;
	}

	if (keybind_settings_capturing()) {
		if (key == SDLK_ESCAPE) {
			keybind_settings_cancel_capture();
			return;
		}
		if (key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LCTRL || key == SDLK_RCTRL || key == SDLK_LALT ||
		    key == SDLK_RALT) {
			return;
		}
		keybind_settings_accept_key(key, mods);
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
		keybind_panel_accept_key(key, mods);
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

	/* movement keys. The classic command line is gone (the tabbed chat
	 * window owns chat), so no key routes to it anymore - Return, Tab and
	 * the editing keys fall through to the binding system instead. */
	switch (key) {
	case SDLK_LEFT:
		keyboard_move_press(KMOVE_LEFT);
		return;
	case SDLK_RIGHT:
		keyboard_move_press(KMOVE_RIGHT);
		return;
	case SDLK_UP:
		keyboard_move_press(KMOVE_UP);
		return;
	case SDLK_DOWN:
		keyboard_move_press(KMOVE_DOWN);
		return;
	default:
		break;
	}

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
		mousex = local_x / sdl_scale - render_offset_x();
		mousey = local_y / sdl_scale - render_offset_y();
		/* GUI coordinates live on the UI layer. Derive from the actual
		 * canvas/layer ratio - the exact inverse of the composite - so the
		 * pointer can never disagree with where the layer really draws. */
		mousex = mousex * UIXRES / XRES;
		mousey = mousey * UIYRES / YRES;

		if (gui_is_loading()) {
			break; /* hover only feeds the menu overlays while loading */
		}

		if (grab.kind == GESTURE_BUTTON) {
			int dx, dy;

			/* the captured button follows the pointer: absolute positions
			 * for the panel gestures, plain deltas for the legacy thumbs */
			gesture_motion(&grab, mousex, mousey, &dx, &dy);
			mousedx += dx;
			mousedy += dy;
			if (but[capbut].flags & BUTF_MOVEEXEC) {
				exec_cmd(lcmd, 0);
			}
			amod_mouse_move(mousex, mousey); /* hover only - the release is ours */
			break;
		}

		if (mm_pan) {
			minimap_pan_update(mousex, mousey);
		}
		amod_mouse_move(mousex, mousey);
		break;

	case SDL_MOUM_LDOWN:
		vk_lbut = 1;

		if (gui_is_loading()) {
			break; /* clicks are handled on release while loading */
		}

		if (grab.kind != GESTURE_NONE) {
			break; /* a press while a gesture is live: nothing new starts */
		}
		lclick_release_pending = 0; /* a fresh press: its release counts again */

		if (amod_mouse_click(mousex, mousey, what)) {
			/* the mod owns this press: it gets every motion and the
			 * release, and the client never acts on that release itself */
			gesture_begin(&grab, GESTURE_MOD, -1, mousex, mousey);
			break;
		}

		/* a press on a spellbook cell picks the spell up right away, so a
		 * press-drag-release onto the hotbar works like any other drag */
		if (spellbook_mousedown(mousex, mousey)) {
			break;
		}

		if (butsel >= BUT_HOTBAR_BEG && butsel <= BUT_HOTBAR_END) {
			hotbar_mousedown(butsel - BUT_HOTBAR_BEG);
		}

		if (butsel != -1 && (but[butsel].flags & BUTF_CAPTURE)) {
			gesture_take_button(butsel);
			break;
		}

		/* a locked minimap cannot be moved, so a drag on its big map pans
		 * the view instead; the recenter glyph takes a plain click */
		if (butsel == BUT_PANEL_BODY && panel_locked(PANEL_MINIMAP)) {
			if (minimap_recenter_hit(mousex, mousey)) {
				minimap_recenter();
				break;
			}
			if (minimap_pan_begin(mousex, mousey)) {
				mm_pan = 1;
				break;
			}
		}

		/* nothing of the client's under the pointer: the mod's background
		 * layer (the chat) gets the press before it would reach the world */
		if (background_may_take() && amod_mouse_click_background(mousex, mousey, what)) {
			gesture_begin(&grab, GESTURE_MOD, -1, mousex, mousey);
			break;
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

		if (mm_pan) {
			mm_pan = 0;
			minimap_pan_end();
			break;
		}

		if (grab.kind == GESTURE_BUTTON) {
			/* a press on the minimap that never moved is a click: flip it
			 * between the small circle and the big map - or, on the
			 * recenter glyph, bring the panned view back to the player */
			int minimap_click = (grab.but == BUT_DRAG_BEG + PANEL_MINIMAP) && !panels_drag_moved();

			/* the client owns this gesture: finish it before anyone else
			 * sees the release - a mod window under the pointer used to
			 * eat it and leave the drag (and a hidden cursor) stuck */
			if (!(but[capbut].flags & BUTF_MOVEEXEC)) {
				exec_cmd(lcmd, 0); /* the purse: the split is taken on release */
			}
			gesture_drop_button(0);
			if (minimap_click) {
				if (minimap_recenter_hit(mousex, mousey)) {
					minimap_recenter();
				} else {
					minimap_toggle_size();
				}
			}
			break;
		}
		if (grab.kind == GESTURE_MOD) {
			/* the mod that took the press gets its release, always */
			amod_mouse_click(mousex, mousey, what);
			gesture_end(&grab);
			break;
		}
		if (lclick_release_pending) {
			/* the tail of an abandoned gesture: not a click on anything */
			lclick_release_pending = 0;
			break;
		}

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
		if (spellbook_click(mousex, mousey)) {
			break;
		}

		/* nothing of the client's took the click: the mod's background
		 * layer (the chat) sees it before it becomes a walk or a look */
		if (background_may_take() && amod_mouse_click_background(mousex, mousey, what)) {
			break;
		}

		if ((tmp = context_key_click()) != CMD_NONE) {
			exec_cmd(tmp, 0);
		} else {
			exec_cmd(lcmd, 0);
		}
		break;

	case SDL_MOUM_RDOWN:
		vk_rbut = 1;
		if (gui_is_loading()) {
			break;
		}
		if (grab.kind == GESTURE_BUTTON) {
			/* right-click abandons a panel drag/resize; the release that
			 * follows must not turn into a look command */
			gesture_drop_button(1);
			rclick_cancelled_gesture = 1;
			break;
		}
		if (amod_mouse_click(mousex, mousey, what)) {
			break;
		}
		/* right-drag on the big minimap pans it (any lock state) */
		if (minimap_pan_begin(mousex, mousey)) {
			mm_pan = 1;
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
		if (background_may_take() && amod_mouse_click_background(mousex, mousey, what)) {
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
		if (mm_pan) {
			mm_pan = 0;
			minimap_pan_end();
			break;
		}
		/* swallow the release of a right-click that cancelled targeting or
		 * abandoned a gesture */
		if (rclick_cancelled_targeting || rclick_cancelled_gesture) {
			rclick_cancelled_targeting = 0;
			rclick_cancelled_gesture = 0;
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
		if (background_may_take() && amod_mouse_click_background(mousex, mousey, what)) {
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

		/* the help / quest-log window pages with the wheel */
		if ((display_help || display_quest) && mousex >= dotx(DOT_HLP) && mousex <= dotx(DOT_HL2) &&
		    mousey >= doty(DOT_HLP) && mousey <= doty(DOT_HL2)) {
			exec_cmd(delta > 0 ? CMD_HELP_PREV : CMD_HELP_NEXT, 0);
			break;
		}

		/* scroll wherever the pointer is inside the window, not just over
		 * the text column - the rails and the first item column used to be
		 * dead zones */
		int wx1, wy1, wx2, wy2;

		if (panel_content_shown(PANEL_SKILLS) && panel_content_rect(PANEL_SKILLS, &wx1, &wy1, &wx2, &wy2) &&
		    mousex >= wx1 && mousex <= wx2 && mousey >= wy1 && mousey <= wy2) { // skill list
			while (delta > 0) {
				set_skloff(0, skloff - 1);
				delta--;
			}
			while (delta < 0) {
				set_skloff(0, skloff + 1);
				delta++;
			}
			break;
		}

		if (panel_content_shown(PANEL_CONTAINER) && panel_content_rect(PANEL_CONTAINER, &wx1, &wy1, &wx2, &wy2) &&
		    mousex >= wx1 && mousex <= wx2 && mousey >= wy1 && mousey <= wy2) { // depot / merchant / grave
			while (delta > 0) {
				set_conoff(0, conoff - 1);
				delta--;
			}
			while (delta < 0) {
				set_conoff(0, conoff + 1);
				delta++;
			}
			break;
		}

		if (panel_content_shown(PANEL_CHAT) && mousex >= dotx(DOT_TXT) && mousex < dotx(DOT_TX2) &&
		    mousey >= doty(DOT_TXT) && mousey < doty(DOT_TX2)) { // chat
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

		if (panel_content_shown(PANEL_INVENTORY) && panel_content_rect(PANEL_INVENTORY, &wx1, &wy1, &wx2, &wy2) &&
		    mousex >= wx1 && mousex <= wx2 && mousey >= wy1 && mousey <= wy2) { // inventory
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

		/* the mod's background layer (the chat scrolls with the wheel) */
		if (background_may_take() && amod_mouse_click_background(0, delta, what)) {
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

/* Called with the left-button state every motion event carries, and with 0
 * when the window loses focus. If the OS says the button is up but no
 * release ever reached us (focus change, dropped event, a compositor that
 * swallowed it), the gesture is finished where the pointer is rather than
 * left running until the next click. */
void gui_sdl_mouse_sync(int lbutton_down)
{
	if (lbutton_down) {
		return;
	}
	if (grab.kind != GESTURE_NONE) {
		gui_sdl_mouseproc(0, 0, SDL_MOUM_LUP);
	} else {
		vk_lbut = 0;
	}
}
