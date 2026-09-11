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
#include "modder/mod_registry.h"
#include "amod/amod_options.h"
#include "modder/modder_private.h"
#include "game/game.h"
#include "game/game_private.h"
#include "client/client.h"
#include "client/client_private.h"
#include "gui/gui.h"
#include "sdl/sdl.h"
#ifdef USE_LUAJIT
#include "scripting/lua_interface.h"
#endif

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
	void (*_amod_frame_background)(void);
	int (*_amod_mouse_click_background)(int x, int y, int what);
	int (*_amod_mouse_over_background)(int x, int y);
	void (*_amod_mouse_capture)(int onoff);
	void (*_amod_areachange)(void);
	int (*_amod_keydown)(SDL_Keycode);
	int (*_amod_keyup)(SDL_Keycode);
	int (*_amod_textinput)(SDL_Keycode);
	void (*_amod_update_hover_texts)(void);
	int (*_amod_client_cmd)(const char *buf);
	int (*_amod_hotbar_activate)(int slot, int mode);
	int (*_amod_text_line)(const char *line);
	void (*_amod_register_keybinds)(void);
	char *(*_amod_version)(void);
	void (*_amod_set_mod_dir)(const char *dir);
	/* Retained so a future reload has something to unload. We deliberately do
	 * NOT SDL_UnloadObject at shutdown: mods spawn threads and register SDL
	 * callbacks, and pulling the library out from under them at exit buys
	 * nothing but crash reports. */
	SDL_SharedObject *handle;
	/* Copied, not aliased: a registry re-scan (#lua_reload) rebuilds the
	 * mod_desc array under us, and a borrowed pointer would then name a
	 * different mod. */
	char id[MOD_ID_LEN];
	int loaded;
};

/* Index 0 is the system mod (bin/amod.*) when present; scanned mods follow in
 * registry order. Sized once at load time - see amod_init(). */
static struct mod *mod;
static int mod_count;
static int mod_capacity;

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
int (*_amod_item_group_match)(int group, uint32_t sprite) = NULL;

char *game_email_main = "<no one>";
char *game_email_cash = "<no one>";
char *game_url = "<nowhere>";

/* Bind the entry points every mod may implement. Optional throughout: a NULL
 * pointer just means the dispatcher skips this mod for that event. */
static void bind_mod(struct mod *m, SDL_SharedObject *h)
{
	void *tmp;

	m->handle = h;
	m->loaded = 1;

	// amod
	if ((tmp = SDL_LoadFunction(h, "amod_init"))) {
		m->_amod_init = (void (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_exit"))) {
		m->_amod_exit = (void (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_gamestart"))) {
		m->_amod_gamestart = (void (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_sprite_config"))) {
		m->_amod_sprite_config = (void (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_frame"))) {
		m->_amod_frame = (void (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_tick"))) {
		m->_amod_tick = (void (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_mouse_move"))) {
		m->_amod_mouse_move = (void (*)(int, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_mouse_click"))) {
		m->_amod_mouse_click = (int (*)(int, int, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_mouse_over"))) {
		m->_amod_mouse_over = (int (*)(int, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_frame_background"))) {
		m->_amod_frame_background = (void (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_mouse_click_background"))) {
		m->_amod_mouse_click_background = (int (*)(int, int, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_mouse_over_background"))) {
		m->_amod_mouse_over_background = (int (*)(int, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_mouse_capture"))) {
		m->_amod_mouse_capture = (void (*)(int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_areachange"))) {
		m->_amod_areachange = (void (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_keydown"))) {
		m->_amod_keydown = (int (*)(SDL_Keycode))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_keyup"))) {
		m->_amod_keyup = (int (*)(SDL_Keycode))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_textinput"))) {
		m->_amod_textinput = (int (*)(SDL_Keycode))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_update_hover_texts"))) {
		m->_amod_update_hover_texts = (void (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_client_cmd"))) {
		m->_amod_client_cmd = (int (*)(const char *))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_hotbar_activate"))) {
		m->_amod_hotbar_activate = (int (*)(int, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_text_line"))) {
		m->_amod_text_line = (int (*)(const char *))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_register_keybinds"))) {
		m->_amod_register_keybinds = (void (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_version"))) {
		m->_amod_version = (char *(*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_set_mod_dir"))) {
		m->_amod_set_mod_dir = (void (*)(const char *))tmp;
	}
}

/* Bind what only the system mod may do: replace client behaviour, claim the
 * SV_MOD packet stream, own the options rows and the game data tables. This
 * used to be "slot 0" - it is now "the mod at bin/amod.*", which is the only
 * place the Steam depot puts one. Nothing under mods/ can reach this.
 */
static void bind_system_mod(SDL_SharedObject *h)
{
	void *tmp;

	if ((tmp = SDL_LoadFunction(h, "amod_process"))) {
		_amod_process = (int (*)(const unsigned char *))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_options_count"))) {
		_amod_options_count = (int (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_option_get"))) {
		_amod_option_get = (int (*)(int, struct amod_option *))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_option_set"))) {
		_amod_option_set = (void (*)(int, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_option_tab"))) {
		_amod_option_tab = (int (*)(int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_escape"))) {
		_amod_escape = (int (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_has_open_window"))) {
		_amod_has_open_window = (int (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_item_group_match"))) {
		_amod_item_group_match = (int (*)(int, uint32_t))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_prefetch"))) {
		_amod_prefetch = (int (*)(const unsigned char *))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_display_skill_line"))) {
		_amod_display_skill_line = (int (*)(int, int, int, int, char *))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "amod_is_playersprite"))) {
		_amod_is_playersprite = (int (*)(int))tmp;
	}

	// client functions
	if ((tmp = SDL_LoadFunction(h, "is_cut_sprite"))) {
		is_cut_sprite = (int (*)(unsigned int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "is_mov_sprite"))) {
		is_mov_sprite = (int (*)(unsigned int, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "is_door_sprite"))) {
		is_door_sprite = (int (*)(unsigned int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "is_yadd_sprite"))) {
		is_yadd_sprite = (int (*)(unsigned int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "no_lighting_sprite"))) {
		no_lighting_sprite = (int (*)(unsigned int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "get_chr_height"))) {
		get_chr_height = (int (*)(unsigned int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "trans_asprite"))) {
		trans_asprite = (unsigned int (*)(map_index_t, unsigned int, tick_t, unsigned char *, unsigned char *,
		    unsigned char *, unsigned char *, unsigned char *, unsigned char *, unsigned short *, unsigned short *,
		    unsigned short *, unsigned short *))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "trans_charno"))) {
		trans_charno = (int (*)(int, int *, int *, int *, int *, int *, int *, int *, int *, int *, int *, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "get_player_sprite"))) {
		get_player_sprite = (int (*)(int, int, int, int, int, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "trans_csprite"))) {
		trans_csprite = (void (*)(map_index_t, struct map *, tick_t))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "get_lay_sprite"))) {
		get_lay_sprite = (int (*)(int, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "get_offset_sprite"))) {
		get_offset_sprite = (int (*)(int, int *, int *))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "additional_sprite"))) {
		additional_sprite = (int (*)(unsigned int, int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "opt_sprite"))) {
		opt_sprite = (unsigned int (*)(unsigned int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "get_skltab_index"))) {
		get_skltab_index = (int (*)(int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "get_skltab_sep"))) {
		get_skltab_sep = (int (*)(int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "get_skltab_show"))) {
		get_skltab_show = (int (*)(int))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "do_display_random"))) {
		do_display_random = (int (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "do_toggle_questlog"))) {
		do_toggle_questlog = (int (*)(void))tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "do_display_help"))) {
		do_display_help = (int (*)(int))tmp;
	}

	// client variables
	if ((tmp = SDL_LoadFunction(h, "game_email_main"))) {
		game_email_main = (char *)tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "game_email_cash"))) {
		game_email_cash = (char *)tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "game_url"))) {
		game_url = (char *)tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "game_rankname"))) {
		game_rankname = (char **)tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "game_rankcount"))) {
		game_rankcount = (int *)tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "game_v_max"))) {
		game_v_max = (int *)tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "game_skill"))) {
		game_skill = (struct skill *)tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "game_skilldesc"))) {
		game_skilldesc = (char **)tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "game_v_profbase"))) {
		game_v_profbase = (int *)tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "game_questlog"))) {
		game_questlog = (struct questlog *)tmp;
	}
	if ((tmp = SDL_LoadFunction(h, "game_questcount"))) {
		game_questcount = (int *)tmp;
	}
}

/* bin/amod.<ext>, relative to the game root like every other client path. */
static void load_system_mod(void)
{
	SDL_SharedObject *h;
	struct mod *m = &mod[mod_count];

#ifdef _WIN32
	const char *fname = "bin\\amod.dll";
#elif defined(SDL_PLATFORM_APPLE)
	const char *fname = "bin/amod.dylib";
#else
	const char *fname = "bin/amod.so";
#endif

	if (!(h = SDL_LoadObject(fname))) {
		// The system mod is expected to exist; report why it failed so a broken
		// Steam install / wrong arch / missing dependency is diagnosable.
		note("mod loader: could not load %s: %s", fname, SDL_GetError());
		return;
	}
	note("mod loader: loaded system mod %s", fname);

	bind_mod(m, h);
	bind_system_mod(h);
	snprintf(m->id, sizeof(m->id), "amod");
	mod_count++;
}

/* Everything discovered under <userdir>/mods/. These get the shared entry
 * points only. */
static void load_scanned_mods(void)
{
	int i, n = mod_registry_scan();

	for (i = 0; i < n && mod_count < mod_capacity; i++) {
		const struct mod_desc *desc = mod_registry_get(i);
		SDL_SharedObject *h;
		struct mod *m;

		if (!desc->enabled) {
			note("mod loader: '%s' is disabled", desc->id);
			continue;
		}
		if (!desc->libpath[0]) {
			continue; /* Lua-only mod, loaded by the scripting subsystem */
		}
		if (!(h = SDL_LoadObject(desc->libpath))) {
			warn("mod loader: could not load %s: %s", desc->libpath, SDL_GetError());
			continue;
		}
		note("mod loader: loaded %s %s (%s)", desc->id, desc->version, desc->libpath);

		m = &mod[mod_count];
		bind_mod(m, h);
		snprintf(m->id, sizeof(m->id), "%s", desc->id);
		/* before amod_init, so a mod can find its own assets from its first
		 * line of code instead of guessing at the working directory */
		if (m->_amod_set_mod_dir) {
			m->_amod_set_mod_dir(desc->dir);
		}
		mod_count++;
	}
}

int amod_init(void)
{
	/* system mod + everything the scan can return */
	mod_capacity = MOD_MAX + 1;
	mod = xmalloc((size_t)mod_capacity * sizeof(*mod), MEM_GLOB);
	memset(mod, 0, (size_t)mod_capacity * sizeof(*mod));

	load_system_mod();
	load_scanned_mods();

	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_init) {
			mod[i]._amod_init();
		}
	}

	// capability probe: tells mods implementing amod_textinput that this
	// client dispatches real text input, so they can stop translating raw
	// keycodes in their own input fields (key 0 never types anything)
	amod_textinput(0);

	return 1;
}

void amod_exit(void)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_exit) {
			mod[i]._amod_exit();
		}
	}
}

void amod_gamestart(void)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_gamestart) {
			mod[i]._amod_gamestart();
		}
	}
#ifdef USE_LUAJIT
	lua_scripting_gamestart();
#endif
}

void amod_sprite_config(void)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_sprite_config) {
			mod[i]._amod_sprite_config();
		}
	}
}

void amod_frame(void)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_frame) {
			mod[i]._amod_frame();
		}
	}
#ifdef USE_LUAJIT
	lua_scripting_frame();
#endif
}

void amod_tick(void)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_tick) {
			mod[i]._amod_tick();
		}
	}
#ifdef USE_LUAJIT
	lua_scripting_tick();
	lua_scripting_check_reload(); // no-op unless -dev; self-throttled to ~1/sec
#endif
}

void amod_mouse_move(int x, int y)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_mouse_move) {
			mod[i]._amod_mouse_move(x, y);
		}
	}
#ifdef USE_LUAJIT
	lua_scripting_mouse_move(x, y);
#endif
}

int amod_mouse_click(int x, int y, int what)
{
	int ret = 0, tmp;
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_mouse_click && (tmp = mod[i]._amod_mouse_click(x, y, what))) {
			if (tmp > 0) {
				return 1;
			} else {
				ret = 1;
			}
		}
	}
#ifdef USE_LUAJIT
	tmp = lua_scripting_mouse_click(x, y, what);
	if (tmp > 0) {
		return 1;
	} else if (tmp < 0) {
		ret = 1;
	}
#endif
	return ret;
}

/* background layer: drawn under every client panel, offered events only when
 * nothing of the client's GUI is under the pointer (see amod.h) */
void amod_frame_background(void)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_frame_background) {
			mod[i]._amod_frame_background();
		}
	}
}

int amod_mouse_click_background(int x, int y, int what)
{
	int ret = 0, tmp;

	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_mouse_click_background && (tmp = mod[i]._amod_mouse_click_background(x, y, what))) {
			if (tmp > 0) {
				return 1;
			}
			ret = 1;
		}
	}
	return ret;
}

int amod_mouse_over_background(int x, int y)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_mouse_over_background && mod[i]._amod_mouse_over_background(x, y)) {
			return 1;
		}
	}
	return 0;
}

int amod_mouse_over(int x, int y)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_mouse_over && mod[i]._amod_mouse_over(x, y)) {
			return 1;
		}
	}
#ifdef USE_LUAJIT
	if (lua_scripting_mouse_over(x, y)) {
		return 1;
	}
#endif
	return 0;
}

void amod_mouse_capture(int onoff)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_mouse_capture) {
			mod[i]._amod_mouse_capture(onoff);
		}
	}
}

void amod_areachange(void)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_areachange) {
			mod[i]._amod_areachange();
		}
	}
#ifdef USE_LUAJIT
	lua_scripting_areachange();
#endif
}

int amod_keydown(SDL_Keycode key)
{
	int ret = 0, tmp;
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_keydown && (tmp = mod[i]._amod_keydown(key))) {
			/* A mod that implements amod_textinput gets the real (layout- and
			 * shift-aware) character through that hook instead - leave the
			 * pending SDL text event queued for it. For older mods the flush
			 * keeps the consumed key's char out of the classic command line. */
			if (!mod[i]._amod_textinput) {
				sdl_flush_textinput();
			}
			if (tmp > 0) {
				return 1;
			} else {
				ret = 1;
			}
		}
	}
#ifdef USE_LUAJIT
	tmp = lua_scripting_keydown(key);
	if (tmp > 0) {
		sdl_flush_textinput();
		return 1;
	} else if (tmp < 0) {
		ret = 1;
	}
#endif
	return ret;
}

// Text input (SDL_EVENT_TEXT_INPUT) - the shifted/layout-correct character,
// dispatched before the classic command line sees it. key==0 is the one-time
// capability probe sent after mod loading so mods can stop translating raw
// keycodes themselves. Same return convention as amod_keydown.
int amod_textinput(SDL_Keycode key)
{
	int ret = 0, tmp;
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_textinput && (tmp = mod[i]._amod_textinput(key))) {
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
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_keyup && (tmp = mod[i]._amod_keyup(key))) {
			if (tmp > 0) {
				return 1;
			} else {
				ret = 1;
			}
		}
	}
#ifdef USE_LUAJIT
	tmp = lua_scripting_keyup(key);
	if (tmp > 0) {
		return 1;
	} else if (tmp < 0) {
		ret = 1;
	}
#endif
	return ret;
}

void amod_update_hover_texts(void)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_update_hover_texts) {
			mod[i]._amod_update_hover_texts();
		}
	}
}

int amod_client_cmd(const char *buf)
{
	int ret = 0, tmp;
#ifdef USE_LUAJIT
	// Check Lua commands first (allows #lua_reload etc. to work)
	tmp = lua_scripting_client_cmd(buf);
	if (tmp > 0) {
		return 1;
	} else if (tmp < 0) {
		ret = 1;
	}
#endif
	for (int i = 0; i < mod_count; i++) {
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

// Called for every chat/system text line just before the classic chat window
// renders it. Any mod may consume the line (e.g. to show it in its own chat
// UI); consumed lines are not added to the classic scrollback. Same return
// convention as the other event handlers: 1 = consumed, stop; -1 = consumed
// but let later mods observe it too; 0 = not handled.
int amod_text_line(const char *line)
{
	int ret = 0, tmp;
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_text_line && (tmp = mod[i]._amod_text_line(line))) {
			if (tmp > 0) {
				return 1;
			} else {
				ret = 1;
			}
		}
	}
	return ret;
}

/* called at the end of register_all(): mods add their own entries to the
 * keybinding table here, BEFORE the per-character config is applied, so
 * player rebinds of mod keys persist like any native binding */
void amod_register_keybinds(void)
{
	for (int i = 0; i < mod_count; i++) {
		if (mod[i]._amod_register_keybinds) {
			mod[i]._amod_register_keybinds();
		}
	}
}

int amod_hotbar_activate(int slot, int mode)
{
	int ret = 0, tmp;
	for (int i = 0; i < mod_count; i++) {
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

int amod_count(void)
{
	return mod_count;
}

// Mod id: the system mod is "amod", scanned mods use their manifest id.
const char *amod_id(int idx)
{
	if (idx < 0 || idx >= mod_count) {
		return NULL;
	}
	return mod[idx].id;
}

// The mod's own amod_version() string, else "unknown". The manifest version is
// only a claim by whoever packaged it; this one comes from the binary itself.
char *amod_version(int idx)
{
	if (idx < 0 || idx >= mod_count) {
		return NULL;
	}

	if (mod[idx]._amod_version) {
		return mod[idx]._amod_version();
	}

	return "unknown";
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

// Does this item sprite belong to the given hotbar item group (potions,
// recall scrolls, ...)? 1 = yes, 0 = no, -1 = no mod table (use the client's
// builtin fallback). The mod owns the id sets since they ship with content.
int amod_item_group_match(int group, uint32_t sprite)
{
	if (_amod_item_group_match) {
		return _amod_item_group_match(group, sprite);
	}
	return -1;
}
