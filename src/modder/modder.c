/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Mod Loading
 *
 * Loads and initializes the amod.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_keycode.h>

#include "astonia.h"
#include "modder/modder.h"
#include "amod/amod_options.h"
#include "modder/modder_private.h"
#include "game/game.h"
#include "game/game_private.h"
#include "client/client.h"
#include "client/client_private.h"
#include "gui/gui.h"
#include "sdl/sdl.h"

struct mod {
	void (*_amod_init)(void);
	void (*_amod_exit)(void);
	void (*_amod_gamestart)(void);
	void (*_amod_sprite_config)(void);
	void (*_amod_frame)(void);
	void (*_amod_tick)(void);
	void (*_amod_mouse_move)(int x, int y);
	int (*_amod_mouse_click)(int x, int y, int what);
	int (*_amod_mouse_over)(int x, int y);
	void (*_amod_mouse_capture)(int onoff);
	void (*_amod_areachange)(void);
	int (*_amod_keydown)(SDL_Keycode);
	int (*_amod_keyup)(SDL_Keycode);
	void (*_amod_update_hover_texts)(void);
	int (*_amod_client_cmd)(const char *buf);
	int (*_amod_hotbar_activate)(int slot, int mode);
	char *(*_amod_version)(void);
	int loaded;
};

struct mod mod[MAXMOD] = {{
    NULL, // _amod_init
    NULL, // _amod_exit
    NULL, // _amod_gamestart
    NULL, // _amod_sprite_config
    NULL, // _amod_frame
    NULL, // _amod_tick
    NULL, // _amod_mouse_move
    NULL, // _amod_mouse_click
    NULL, // _amod_mouse_over
    NULL, // _amod_mouse_capture
    NULL, // _amod_areachange
    NULL, // _amod_keydown
    NULL, // _amod_keyup
    NULL, // _amod_update_hover_texts
    NULL, // _amod_client_cmd
    NULL, // _amod_hotbar_activate
    NULL, // _amod_version
    0 // loaded
}};

int (*_amod_is_playersprite)(int sprite) = NULL;
int (*_amod_display_skill_line)(int v, int base, int curr, int cn, char *buf) = NULL;
int (*_amod_process)(const unsigned char *buf) = NULL;
int (*_amod_prefetch)(const unsigned char *buf) = NULL;
int (*_amod_options_count)(void) = NULL;
int (*_amod_option_get)(int index, struct amod_option *out) = NULL;
void (*_amod_option_set)(int index, int value) = NULL;
int (*_amod_option_tab)(int index) = NULL;
int (*_amod_escape)(void) = NULL;
int (*_amod_has_open_window)(void) = NULL;

char *game_email_main = "<no one>";
char *game_email_cash = "<no one>";
char *game_url = "<nowhere>";

int amod_init(void)
{
	void *dll_instance = NULL;
	void *tmp;
	char fname[80];

	for (int i = 0; i < MAXMOD; i++) {
#ifdef _WIN32
		sprintf(fname, "bin\\%cmod.dll", i + 'a');
#elif defined(SDL_PLATFORM_APPLE)
		sprintf(fname, "bin/%cmod.dylib", i + 'a');
#else
		sprintf(fname, "bin/%cmod.so", i + 'a');
#endif
		dll_instance = SDL_LoadObject(fname);
		if (!dll_instance) {
			// Only the first slot (amod) is expected to exist; report why it failed to load
			// so a broken Steam install / wrong arch / missing dependency is diagnosable.
			if (i == 0) {
				note("mod loader: could not load %s: %s", fname, SDL_GetError());
			}
			continue;
		};
		note("mod loader: loaded %s", fname);

		mod[i].loaded = 1;

		// amod
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_init"))) {
			mod[i]._amod_init = (void (*)(void))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_exit"))) {
			mod[i]._amod_exit = (void (*)(void))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_gamestart"))) {
			mod[i]._amod_gamestart = (void (*)(void))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_sprite_config"))) {
			mod[i]._amod_sprite_config = (void (*)(void))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_frame"))) {
			mod[i]._amod_frame = (void (*)(void))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_tick"))) {
			mod[i]._amod_tick = (void (*)(void))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_mouse_move"))) {
			mod[i]._amod_mouse_move = (void (*)(int, int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_mouse_click"))) {
			mod[i]._amod_mouse_click = (int (*)(int, int, int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_mouse_over"))) {
			mod[i]._amod_mouse_over = (int (*)(int, int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_mouse_capture"))) {
			mod[i]._amod_mouse_capture = (void (*)(int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_areachange"))) {
			mod[i]._amod_areachange = (void (*)(void))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_keydown"))) {
			mod[i]._amod_keydown = (int (*)(SDL_Keycode))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_keyup"))) {
			mod[i]._amod_keyup = (int (*)(SDL_Keycode))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_update_hover_texts"))) {
			mod[i]._amod_update_hover_texts = (void (*)(void))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_client_cmd"))) {
			mod[i]._amod_client_cmd = (int (*)(const char *))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_hotbar_activate"))) {
			mod[i]._amod_hotbar_activate = (int (*)(int, int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_version"))) {
			mod[i]._amod_version = (char *(*)(void))tmp;
		}
		if (i != 0) {
			continue; // only amod is allowed to override client stuff, the others can only add stuff
		}

		if ((tmp = SDL_LoadFunction(dll_instance, "amod_process"))) {
			_amod_process = (int (*)(const unsigned char *))tmp;
		}
		if (!_amod_options_count && (tmp = SDL_LoadFunction(dll_instance, "amod_options_count"))) {
			_amod_options_count = (int (*)(void))tmp;
		}
		if (!_amod_option_get && (tmp = SDL_LoadFunction(dll_instance, "amod_option_get"))) {
			_amod_option_get = (int (*)(int, struct amod_option *))tmp;
		}
		if (!_amod_option_set && (tmp = SDL_LoadFunction(dll_instance, "amod_option_set"))) {
			_amod_option_set = (void (*)(int, int))tmp;
		}
		if (!_amod_option_tab && (tmp = SDL_LoadFunction(dll_instance, "amod_option_tab"))) {
			_amod_option_tab = (int (*)(int))tmp;
		}
		if (!_amod_escape && (tmp = SDL_LoadFunction(dll_instance, "amod_escape"))) {
			_amod_escape = (int (*)(void))tmp;
		}
		if (!_amod_has_open_window && (tmp = SDL_LoadFunction(dll_instance, "amod_has_open_window"))) {
			_amod_has_open_window = (int (*)(void))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_prefetch"))) {
			_amod_prefetch = (int (*)(const unsigned char *))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_display_skill_line"))) {
			_amod_display_skill_line = (int (*)(int, int, int, int, char *))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "amod_is_playersprite"))) {
			_amod_is_playersprite = (int (*)(int))tmp;
		}

		// client functions
		if ((tmp = SDL_LoadFunction(dll_instance, "is_cut_sprite"))) {
			is_cut_sprite = (int (*)(unsigned int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "is_mov_sprite"))) {
			is_mov_sprite = (int (*)(unsigned int, int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "is_door_sprite"))) {
			is_door_sprite = (int (*)(unsigned int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "is_yadd_sprite"))) {
			is_yadd_sprite = (int (*)(unsigned int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "get_chr_height"))) {
			get_chr_height = (int (*)(unsigned int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "trans_asprite"))) {
			trans_asprite = (unsigned int (*)(map_index_t, unsigned int, tick_t, unsigned char *, unsigned char *,
			    unsigned char *, unsigned char *, unsigned char *, unsigned char *, unsigned short *, unsigned short *,
			    unsigned short *, unsigned short *))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "trans_charno"))) {
			trans_charno = (int (*)(int, int *, int *, int *, int *, int *, int *, int *, int *, int *, int *, int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "get_player_sprite"))) {
			get_player_sprite = (int (*)(int, int, int, int, int, int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "trans_csprite"))) {
			trans_csprite = (void (*)(map_index_t, struct map *, tick_t))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "get_lay_sprite"))) {
			get_lay_sprite = (int (*)(int, int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "get_offset_sprite"))) {
			get_offset_sprite = (int (*)(int, int *, int *))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "additional_sprite"))) {
			additional_sprite = (int (*)(unsigned int, int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "opt_sprite"))) {
			opt_sprite = (unsigned int (*)(unsigned int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "get_skltab_index"))) {
			get_skltab_index = (int (*)(int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "get_skltab_sep"))) {
			get_skltab_sep = (int (*)(int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "get_skltab_show"))) {
			get_skltab_show = (int (*)(int))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "do_display_random"))) {
			do_display_random = (int (*)(void))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "do_toggle_questlog"))) {
			do_toggle_questlog = (int (*)(void))tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "do_display_help"))) {
			do_display_help = (int (*)(int))tmp;
		}

		// client variables
		if ((tmp = SDL_LoadFunction(dll_instance, "game_email_main"))) {
			game_email_main = (char *)tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "game_email_cash"))) {
			game_email_cash = (char *)tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "game_url"))) {
			game_url = (char *)tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "game_rankname"))) {
			game_rankname = (char **)tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "game_rankcount"))) {
			game_rankcount = (int *)tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "game_v_max"))) {
			game_v_max = (int *)tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "game_skill"))) {
			game_skill = (struct skill *)tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "game_skilldesc"))) {
			game_skilldesc = (char **)tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "game_v_profbase"))) {
			game_v_profbase = (int *)tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "game_questlog"))) {
			game_questlog = (struct questlog *)tmp;
		}
		if ((tmp = SDL_LoadFunction(dll_instance, "game_questcount"))) {
			game_questcount = (int *)tmp;
		}
	}

	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_init) {
			mod[i]._amod_init();
		}
	}

	return 1;
}

void amod_exit(void)
{
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_exit) {
			mod[i]._amod_exit();
		}
	}
}

void amod_gamestart(void)
{
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_gamestart) {
			mod[i]._amod_gamestart();
		}
	}
}

void amod_sprite_config(void)
{
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_sprite_config) {
			mod[i]._amod_sprite_config();
		}
	}
}

void amod_frame(void)
{
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_frame) {
			mod[i]._amod_frame();
		}
	}
}

void amod_tick(void)
{
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_tick) {
			mod[i]._amod_tick();
		}
	}
}

void amod_mouse_move(int x, int y)
{
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_mouse_move) {
			mod[i]._amod_mouse_move(x, y);
		}
	}
}

int amod_mouse_click(int x, int y, int what)
{
	int ret = 0, tmp;
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_mouse_click && (tmp = mod[i]._amod_mouse_click(x, y, what))) {
			if (tmp > 0) {
				return 1;
			} else {
				ret = 1;
			}
		}
	}
	return ret;
}

int amod_mouse_over(int x, int y)
{
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_mouse_over && mod[i]._amod_mouse_over(x, y)) {
			return 1;
		}
	}
	return 0;
}

void amod_mouse_capture(int onoff)
{
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_mouse_capture) {
			mod[i]._amod_mouse_capture(onoff);
		}
	}
}

void amod_areachange(void)
{
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_areachange) {
			mod[i]._amod_areachange();
		}
	}
}

int amod_keydown(SDL_Keycode key)
{
	int ret = 0, tmp;
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_keydown && (tmp = mod[i]._amod_keydown(key))) {
			sdl_flush_textinput();
			if (tmp > 0) {
				return 1;
			} else {
				ret = 1;
			}
		}
	}
	return ret;
}

int amod_keyup(SDL_Keycode key)
{
	int ret = 0, tmp;
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_keyup && (tmp = mod[i]._amod_keyup(key))) {
			if (tmp > 0) {
				return 1;
			} else {
				ret = 1;
			}
		}
	}
	return ret;
}

void amod_update_hover_texts(void)
{
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_update_hover_texts) {
			mod[i]._amod_update_hover_texts();
		}
	}
}

int amod_client_cmd(const char *buf)
{
	int ret = 0, tmp;
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_client_cmd && (tmp = mod[i]._amod_client_cmd(buf))) {
			if (tmp > 0) {
				return 1;
			} else {
				ret = 1;
			}
		}
	}
	return ret;
}

int amod_hotbar_activate(int slot, int mode)
{
	int ret = 0, tmp;
	for (int i = 0; i < MAXMOD; i++) {
		if (mod[i]._amod_hotbar_activate && (tmp = mod[i]._amod_hotbar_activate(slot, mode))) {
			if (tmp > 0) {
				return 1;
			} else {
				ret = 1;
			}
		}
	}
	return ret;
}

int amod_display_skill_line(int v, int base, int curr, int cn, char *buf)
{
	if (_amod_display_skill_line) {
		return _amod_display_skill_line(v, base, curr, cn, buf);
	}
	return 0;
}

int amod_process(const unsigned char *buf)
{
	if (_amod_process) {
		return _amod_process(buf);
	}
	return 0;
}

int amod_prefetch(const unsigned char *buf)
{
	if (_amod_prefetch) {
		return _amod_prefetch(buf);
	}
	return 0;
}

int amod_is_playersprite(int sprite)
{
	if (_amod_is_playersprite) {
		return _amod_is_playersprite(sprite);
	}
	return 0;
}

char *amod_version(int idx)
{
	if (idx < 0 || idx >= MAXMOD) {
		return NULL;
	}

	if (mod[idx]._amod_version) {
		return mod[idx]._amod_version();
	}

	if (mod[idx].loaded) {
		return "unknown";
	}

	return NULL;
}

// True if a loaded mod handles server mod packets (i.e. the Ugaris mod is present).
int amod_main_loaded(void)
{
	return _amod_process != NULL;
}

// Called by the protocol layer when a SV_MOD packet was skipped because no mod
// claimed it. Logged once per (type,subtype) pair to avoid spamming.
void amod_note_unhandled(int type, int subtype)
{
	static unsigned char seen[5][256];

	if (type < SV_MOD1 || type > SV_MOD5 || subtype < 0 || subtype > 255) {
		return;
	}
	if (seen[type - SV_MOD1][subtype]) {
		return;
	}
	seen[type - SV_MOD1][subtype] = 1;
	note("mod: skipped unhandled server mod packet type %d subtype 0x%02X%s", type, subtype,
	    _amod_process ? "" : " (no mod loaded)");
}

int amod_options_count(void)
{
	if (_amod_options_count && _amod_option_get) {
		return _amod_options_count();
	}
	return 0;
}

int amod_option_get(int index, struct amod_option *out)
{
	if (_amod_option_get && out) {
		return _amod_option_get(index, out);
	}
	return 0;
}

void amod_option_set(int index, int value)
{
	if (_amod_option_set) {
		_amod_option_set(index, value);
	}
}

int amod_option_tab(int index)
{
	if (_amod_option_tab) {
		int tab = _amod_option_tab(index);
		if (tab >= AMOD_TAB_GAMEPLAY && tab <= AMOD_TAB_UI) {
			return tab;
		}
	}
	return AMOD_TAB_GAMEPLAY;
}

// ESC pressed: give the mod a chance to close its topmost window. Returns 1
// if the mod consumed the key. Optional export - older mods simply never see
// ESC, as before.
int amod_escape(void)
{
	if (_amod_escape) {
		return _amod_escape();
	}
	return 0;
}

// True if the mod currently shows a window ESC should close (journal, auction
// house, ...). Feeds the client's anything_to_cancel().
int amod_has_open_window(void)
{
	if (_amod_has_open_window) {
		return _amod_has_open_window();
	}
	return 0;
}
