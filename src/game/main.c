/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Startup And Command Line
 *
 * Contains the startup stuff and the parsing of the command line. Plus a
 * bunch of generic helper for memory allocation and error display.
 *
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <ctype.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_keycode.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "astonia.h"
#include "game/game.h"
#include "game/game_private.h"
#include "game/sprite_config.h"
#include "sdl/sdl.h"
#include "sdl/sdl_gpu.h"
#include "sdl/sdl_gpu_shaderfx.h"
#include "sdl/font_manager.h"
#include "gui/gui.h"
#include "gui/loading_ui.h"
#include "gui/input_bind.h"
#include "gui/options_ui.h"
#include "client/client.h"
#include "lib/cjson/cJSON.h"
#include "modder/modder.h"
#ifdef USE_LUAJIT
#include "scripting/lua_interface.h"
#endif

// Forward declarations
void xlog(FILE *logfp, char *format, ...) __attribute__((format(printf, 2, 3)));
void addlinesep(void);
int rread(FILE *fp, void *ptr, size_t size);
void rrandomize(void);
void display_usage(void);
int parse_args(int argc, char *argv[]);
void load_options(void);
void init_logging(void);
void determine_resolution(void);

int quit = 0;
int sv_ver = 30;

char *localdata;

static int panic_reached = 0;
int xmemcheck_failed = 0;
SDL_Keycode user_keys[10] = {'Q', 'W', 'E', 'A', 'S', 'D', 'Z', 'X', 'C', 'V'};

DLL_EXPORT uint64_t game_options = GO_NOTSET;

static char memcheck_failed_str[] = {"memcheck failed"};
static char panic_reached_str[] = {"panic failure"};

FILE *errorfp;

// note, warn, fail, paranoia, addline

DLL_EXPORT int note(const char *format, ...)
{
	va_list va;
	char buf[1024];


	va_start(va, format);
	vsprintf(buf, format, va);
	va_end(va);

	printf("NOTE: %s\n", buf);
	fflush(stdout);
#ifdef DEVELOPER
	addline("NOTE: %s\n", buf);
#endif

	return 0;
}

DLL_EXPORT int warn(const char *format, ...)
{
	va_list va;
	char buf[1024];


	va_start(va, format);
	vsprintf(buf, format, va);
	va_end(va);

	printf("WARN: %s\n", buf);
	fflush(stdout);
	addline("WARN: %s\n", buf);

	return 0;
}

DLL_EXPORT int fail(const char *format, ...)
{
	va_list va;
	char buf[1024];


	va_start(va, format);
	vsprintf(buf, format, va);
	va_end(va);

	fprintf(errorfp, "FAIL: %s\n", buf);
	fflush(errorfp);
	printf("FAIL: %s\n", buf);
	fflush(stdout);
	addline("FAIL: %s\n", buf);

	return -1;
}

DLL_EXPORT void paranoia(const char *format, ...)
{
	va_list va;

	fprintf(errorfp, "PARANOIA EXIT in ");

	va_start(va, format);
	vfprintf(errorfp, format, va);
	va_end(va);

	fprintf(errorfp, "\n");
	fflush(errorfp);

	exit(-1);
}

void xlog(FILE *logfp, char *format, ...)
{
	va_list args;
	char buf[1024];
	struct tm *tm;
	time_t time_now;
	time(&time_now);

	va_start(args, format);
	vsnprintf(buf, 1024, format, args);
	va_end(args);

	tm = localtime(&time_now);
	if (tm) {
		fprintf(logfp, "%02d.%02d.%02d %02d:%02d:%02d: %s\n", tm->tm_mday, tm->tm_mon + 1, tm->tm_year - 100,
		    tm->tm_hour, tm->tm_min, tm->tm_sec, buf);
	} else {
		fprintf(logfp, "%s\n", buf);
	}
	fflush(logfp);
}

static int _addlinesep = 0;

void addlinesep(void)
{
	_addlinesep = 1;
}

DLL_EXPORT void addline(const char *format, ...)
{
	va_list va;
	char buf[1024];

	if (_addlinesep) {
		_addlinesep = 0;
		addline("-------------");
	}

	va_start(va, format);
	vsnprintf(buf, sizeof(buf) - 1, format, va);
	buf[sizeof(buf) - 1] = 0;
	va_end(va);

	/* every chat/system line funnels through here - server text, client
	 * notices and mod command feedback alike - so this is where the tabbed
	 * chat gets its copy (and may consume the line so the classic window
	 * stays quiet). Guarded: a mod printing from inside its own text hook
	 * must not recurse. */
	{
		static int in_hook;

		if (!in_hook) {
			int eaten;

			in_hook = 1;
			eaten = amod_text_line(buf);
			in_hook = 0;
			if (eaten) {
				return;
			}
		}
	}

	if (render_text_init_done()) {
		render_add_text(buf);
	}
}

// io

int rread(FILE *fp, void *ptr, size_t size)
{
	size_t n;

	while (size > 0) {
		n = fread(ptr, 1, size, fp);
		if (n == 0) {
			return 1;
		}
		size -= n;
		ptr = ((unsigned char *)(ptr)) + n;
	}
	return 0;
}

char *load_ascii_file(const char *filename, uint8_t ID)
{
	FILE *fp;
	size_t size;
	char *ptr;

	if (!(fp = fopen(filename, "rb"))) {
		return NULL;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return NULL;
	}
	{
		long file_size = ftell(fp);
		if (file_size < 0) {
			fclose(fp);
			return NULL;
		}
		size = (size_t)file_size;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return NULL;
	}
	ptr = xmalloc(size + 1, ID);
	if (rread(fp, ptr, size)) {
		xfree(ptr);
		fclose(fp);
		return NULL;
	}
	ptr[size] = 0;
	fclose(fp);

	return ptr;
}

// rrandom

void rrandomize(void)
{
	srand((unsigned int)time(NULL));
}

int rrand(int range)
{
	return rand() % range;
}

// parsing command line

void display_messagebox(char *title, char *text)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, NULL);
}

void display_usage(void)
{
	const char *help =
	    "The Astonia Client can only be started from the command line or with a specially created shortcut.\n\n"
	    "Usage: moac -u playername -p password -d url\n ... [-w width] [-h height]\n"
	    " ... [-m threads] [-o options]\n ... [-k framespersecond]\n\n"
	    "url being, for example, \"server.astonia.com\" or \"192.168.77.132\" (without the quotes).\n\n"
	    "width and height are the desired window size. If this matches the desktop size the client "
	    "will start in windowed borderless pseudo-fullscreen mode.\n\n"
	    "threads is the number of background threads the game should use. Use 0 to disable. Default is 4.\n\n"
	    "options is a bitfield.\nBit 0 (value of 1) enables the Dark GUI by Tegra.\n"
	    "Bit 1 enables the context menu.\nBit 2 the new keybindings.\nBit 3 the smaller bottom GUI.\n"
	    "Bit 4 the sliding away of the top GUI.\nBit 5 enables the bigger health/mana bars.\n"
	    "Bit 6 enables sound.\nBit 7 the large font.\nBit 8 true full screen mode.\nBit 9 enables the "
	    "legacy mouse wheel logic.\n"
	    "Bit 10 enables out-of-order execution (read: faster) of inventory access and command feedback.\n"
	    "Bit 11 reduces the animation buffer for faster reactions and more stutter.\n"
	    "Bit 12 writes application files to %appdata% instead of the current folder.\n"
	    "Bit 13 enables the loading and saving of minimaps.\n"
	    "Bit 14 and 15 increase gamma.\n"
	    "Bit 16 makes the sliding top bar less sensitive.\n"
	    "Bit 17 reduces lighting effects (more performance, less pretty).\n"
	    "Bit 18 disables the minimap.\n"
	    "Default depends on screen height.\n\n"
	    "framespersecond will set the display rate in frames per second.\n\n";

	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Usage", help, NULL);
	printf("%s", help);
}

DLL_EXPORT char server_url[256];
DLL_EXPORT int server_port = 0;
static int dev_mode = 0; // -dev flag: developer conveniences (Lua auto-reload)
DLL_EXPORT int want_width = 0;
DLL_EXPORT int want_height = 0;
DLL_EXPORT int want_monitor = 0; // Monitor number for multi-monitor support (0=default)

int parse_args(int argc, char *argv[])
{
	int i;
	char *end;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (arg[0] != '-') {
			continue;
		}

		// Long flags first: they must not fall through to the single-letter
		// parser below ("-dev" would otherwise be read as -d "ev").
		if (!strcmp(arg, "-dev") || !strcmp(arg, "--dev")) {
			dev_mode = 1;
			continue;
		}

		char opt = (char)tolower(arg[1]);
		char *val = NULL;

		if (arg[2] != '\0') {
			val = &arg[2];
		} else if (i + 1 < argc) {
			// Every option takes a value: consume the next argv entry now so a value
			// that happens to start with '-' (e.g. a password "-w12") is not re-parsed
			// as an option on the next iteration.
			val = argv[++i];
		}

		switch (opt) {
		case 'u':
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			if (val) {
				snprintf(username, sizeof(username), "%s", val);
			}
			break;
		case 'p':
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			if (val) {
				snprintf(password, sizeof(password), "%s", val);
			}
			break;
		case 'd':
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			if (val) {
				snprintf(server_url, sizeof(server_url), "%s", val);
			}
			break;
		case 'h':
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			if (val) {
				long h = strtol(val, &end, 10);
				if (h < INT_MIN || h > INT_MAX) {
					want_height = 0;
				} else {
					want_height = (int)h;
				}
			}
			break;
		case 'w':
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			if (val) {
				long w = strtol(val, &end, 10);
				if (w < INT_MIN || w > INT_MAX) {
					want_width = 0;
				} else {
					want_width = (int)w;
				}
			}
			break;
		case 'm':
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			if (val) {
				long m = strtol(val, &end, 10);
				if (m < INT_MIN || m > INT_MAX) {
					sdl_multi = 0;
				} else {
					sdl_multi = (int)m;
				}
			}
			break;
		case 'o':
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			if (val) {
				game_options = strtoull(val, &end, 10);
			}
			break;
		case 'c':
			// Legacy flag: cache size is now statically allocated (MAX_TEXCACHE = 8000).
			// Accept but ignore this flag for backward compatibility.
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			// Silently ignored - cache is compile-time fixed
			break;
		case 'k':
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			if (val) {
				long f = strtol(val, &end, 10);
				if (f < INT_MIN || f > INT_MAX) {
					frames_per_second = 0;
				} else {
					frames_per_second = (int)f;
				}
			}
			break;
		case 't':
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			if (val) {
				long p = strtol(val, &end, 10);
				if (p < INT_MIN || p > INT_MAX) {
					server_port = 0;
				} else {
					server_port = (int)p;
				}
			}
			break;
		case 'v':
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			if (val) {
				long v = strtol(val, &end, 10);
				if (v < INT_MIN || v > INT_MAX) {
					sv_ver = 30;
				} else {
					sv_ver = (int)v;
				}
			}
			break;
		case 'n': // -n monitor number for multi-monitor support
			if (!val && i + 1 < argc) {
				val = argv[++i];
			}
			if (val) {
				long n = strtol(val, &end, 10);
				if (n < INT_MIN || n > INT_MAX) {
					want_monitor = 0;
				} else {
					want_monitor = (int)n;
				}
			}
			break;
		default:
			// Unknown option, ignore or warn?
			break;
		}
	}
	return 0;
}

static char active_charname[80];

static const char *get_character_name(void)
{
	if (active_charname[0]) {
		return active_charname;
	}
	return NULL;
}

static void get_config_path(char *buf, size_t bufsize)
{
	const char *charname = get_character_name();
	if (charname && localdata) {
		snprintf(buf, bufsize, "%skeybinds_%s.json", localdata, charname);
	} else if (charname) {
		snprintf(buf, bufsize, "res/config/keybinds_%s.json", charname);
	} else if (localdata) {
		snprintf(buf, bufsize, "%skeybinds.json", localdata);
	} else {
		snprintf(buf, bufsize, "res/config/keybinds.json");
	}
}

static void get_shared_config_path(char *buf, size_t bufsize)
{
	if (localdata) {
		snprintf(buf, bufsize, "%skeybinds.json", localdata);
	} else {
		snprintf(buf, bufsize, "res/config/keybinds.json");
	}
}

static void get_legacy_config_path(char *buf, size_t bufsize)
{
	if (localdata) {
		snprintf(buf, bufsize, "%s%s", localdata, sv_ver == 35 ? "moac35.dat" : "moac.dat");
	} else {
		snprintf(buf, bufsize, "bin/data/%s", sv_ver == 35 ? "moac35.dat" : "moac.dat");
	}
}

/* ── Extra game-option persistence ──────────────────────────────────────
 *
 * The keybind config (input_bind.c) only persists the GO_ bits the
 * launcher passes via -o (see GO_UI_MANAGED there). Newer UI-only toggles
 * like GO_NOLAG live in a small side file instead, so they survive
 * restarts without launcher or keybind-config changes. */

static void get_extra_options_path(char *buf, size_t bufsize)
{
	if (localdata) {
		snprintf(buf, bufsize, "%soptions_extra.json", localdata);
	} else {
		snprintf(buf, bufsize, "res/config/options_extra.json");
	}
}

/* Window mode (0 windowed / 1 borderless / 2 exclusive) the user picked in
 * Options > Video; -1 until they ever touch it. Re-applied after sdl_init. */
int saved_window_mode = -1;
/* VSync from the extra-options file; applied after sdl_init (which forces 1). */
static int saved_vsync = -1;
/* TTF text from the extra-options file; re-applied after sdl_init because
 * sdl_init resets game_options to GO_DEFAULTS when no -o was given. */
static int saved_ttf_text = -1;
/* SDL_GPU renderer (experimental, opt-in) from the extra-options file; must
 * be known BEFORE sdl_init (renderer creation), re-applied to game_options
 * after it (same GO_DEFAULTS reset as the TTF option). */
static int saved_gpu_rendering = -1;
/* GPU shader effects sub-flag (experimental, opt-in, needs gpu_rendering):
 * base textures + per-draw effects in the fragment shader. */
static int saved_gpu_shaderfx = -1;
/* Bump when a *_requested default flips, so configs written under the old
 * default do not pin the old behaviour forever. Everything the client saves
 * is written on every options change, so an experimental feature that was
 * default-off left an explicit "false" in nearly every existing config even
 * for players who never chose it - without this, flipping the default would
 * reach nobody. A config older than the current revision has its GPU keys
 * treated as unset (the new defaults apply); an opt-out made afterwards is
 * written with the current revision and sticks. */
#define GPU_DEFAULTS_REV 1
static int saved_defaults_rev = 0;

/* GPU effect glows (needs gpu_rendering). Default ON: held as the negative
 * GO_NOFANCYFX bit so nothing has to opt in, checked per draw so the
 * Options toggle takes effect immediately. */
static int saved_gpu_fancyfx = -1;

static void save_extra_options(void)
{
	char path[MAX_PATH];
	FILE *fp;
	cJSON *root;
	char *json;

	get_extra_options_path(path, sizeof(path));
	root = cJSON_CreateObject();
	if (!root) {
		return;
	}
	cJSON_AddBoolToObject(root, "hide_lag_warning", (game_options & GO_NOLAG) != 0);
	cJSON_AddBoolToObject(root, "ttf_text", (game_options & GO_TTF) != 0);
	cJSON_AddBoolToObject(root, "gpu_rendering", (game_options & GO_GPU) != 0);
	cJSON_AddBoolToObject(root, "gpu_shader_effects", (game_options & GO_SHADERFX) != 0);
	cJSON_AddBoolToObject(root, "gpu_fancy_effects", (game_options & GO_NOFANCYFX) == 0);
	cJSON_AddNumberToObject(root, "defaults_rev", GPU_DEFAULTS_REV);
	cJSON_AddNumberToObject(root, "master_volume", sound_volume);
	cJSON_AddNumberToObject(root, "sfx_volume", sound_volume_sfx);
	cJSON_AddNumberToObject(root, "ambient_volume", sound_volume_ambient);
	cJSON_AddNumberToObject(root, "ui_volume", sound_volume_ui);
	cJSON_AddNumberToObject(root, "fps_limit", frames_per_second);
	cJSON_AddNumberToObject(root, "vsync", sdl_vsync);
	cJSON_AddNumberToObject(root, "texture_cache", sdl_cache_size);
	cJSON_AddNumberToObject(root, "worker_threads", sdl_multi);
	cJSON_AddNumberToObject(root, "window_mode", saved_window_mode);

	json = cJSON_Print(root);
	cJSON_Delete(root);
	if (!json) {
		return;
	}
	fp = fopen(path, "w");
	if (fp) {
		fputs(json, fp);
		fputc('\n', fp);
		fclose(fp);
	}
	cJSON_free(json);
}

static int extra_int(cJSON *root, const char *key, int fallback, int min_val, int max_val)
{
	cJSON *v = cJSON_GetObjectItem(root, key);

	if (!v || !cJSON_IsNumber(v)) {
		return fallback;
	}
	if (v->valueint < min_val) {
		return min_val;
	}
	if (v->valueint > max_val) {
		return max_val;
	}
	return v->valueint;
}

static void load_extra_options(void)
{
	char path[MAX_PATH];
	char *json;
	cJSON *root, *v;

	get_extra_options_path(path, sizeof(path));
	json = load_ascii_file(path, MEM_TEMP);
	if (!json && localdata) {
		/* one-time migration: earlier builds (launcher without GO_APPDATA)
		 * kept this file inside the install dir */
		json = load_ascii_file("res/config/options_extra.json", MEM_TEMP);
	}
	if (!json) {
		return;
	}
	root = cJSON_Parse(json);
	xfree(json);
	if (!root) {
		return;
	}
	v = cJSON_GetObjectItem(root, "hide_lag_warning");
	if (v && cJSON_IsBool(v)) {
		if (cJSON_IsTrue(v)) {
			game_options |= GO_NOLAG;
		} else {
			game_options &= ~GO_NOLAG;
		}
	}
	v = cJSON_GetObjectItem(root, "ttf_text");
	if (v && cJSON_IsBool(v)) {
		saved_ttf_text = cJSON_IsTrue(v) ? 1 : 0;
		if (saved_ttf_text) {
			game_options |= GO_TTF;
		} else {
			game_options &= ~GO_TTF;
		}
	}
	v = cJSON_GetObjectItem(root, "gpu_rendering");
	if (v && cJSON_IsBool(v)) {
		saved_gpu_rendering = cJSON_IsTrue(v) ? 1 : 0;
		if (saved_gpu_rendering) {
			game_options |= GO_GPU;
		} else {
			game_options &= ~GO_GPU;
		}
	}
	v = cJSON_GetObjectItem(root, "gpu_shader_effects");
	if (v && cJSON_IsBool(v)) {
		saved_gpu_shaderfx = cJSON_IsTrue(v) ? 1 : 0;
		if (saved_gpu_shaderfx) {
			game_options |= GO_SHADERFX;
		} else {
			game_options &= ~GO_SHADERFX;
		}
	}
	/* effect glows: stored as a positive key but held as a negative bit, so
	 * an absent key (and any -o mask that predates it) means glows ON */
	v = cJSON_GetObjectItem(root, "gpu_fancy_effects");
	if (v && cJSON_IsBool(v)) {
		saved_gpu_fancyfx = cJSON_IsTrue(v) ? 1 : 0;
		if (saved_gpu_fancyfx) {
			game_options &= ~GO_NOFANCYFX;
		} else {
			game_options |= GO_NOFANCYFX;
		}
	}
	saved_defaults_rev = extra_int(root, "defaults_rev", 0, 0, 1000000);
	if (saved_defaults_rev < GPU_DEFAULTS_REV) {
		/* written while these were experimental opt-ins; adopt the new
		 * defaults rather than inheriting a "false" nobody chose */
		saved_gpu_rendering = -1;
		saved_gpu_shaderfx = -1;
		game_options |= GO_GPU;
		game_options |= GO_SHADERFX;
	}
	sound_volume = extra_int(root, "master_volume", sound_volume, 0, 128);
	sound_volume_sfx = extra_int(root, "sfx_volume", sound_volume_sfx, 0, 128);
	sound_volume_ambient = extra_int(root, "ambient_volume", sound_volume_ambient, 0, 128);
	sound_volume_ui = extra_int(root, "ui_volume", sound_volume_ui, 0, 128);
	frames_per_second = extra_int(root, "fps_limit", frames_per_second, 24, 244);
	saved_vsync = extra_int(root, "vsync", -1, 0, 1);
	sdl_cache_size = extra_int(root, "texture_cache", sdl_cache_size, 1000, 8000);
	sdl_multi = extra_int(root, "worker_threads", sdl_multi, 1, 8);
	saved_window_mode = extra_int(root, "window_mode", -1, -1, 2);
	cJSON_Delete(root);
}

void save_options(void)
{
	char path[MAX_PATH];
	get_config_path(path, sizeof(path));
	input_save_config(path);
	save_extra_options();
}

void load_options(void)
{
	char path[MAX_PATH];

	active_charname[0] = '\0';
	input_init(sv_ver);
	load_extra_options();

	get_shared_config_path(path, sizeof(path));
	if (input_load_config(path) == 0) {
		return;
	}

	/* One-time migration: earlier builds (launcher without GO_APPDATA) kept
	 * configs inside the install dir, where Steam updates could wipe them.
	 * Pull an existing install-dir config into the pref path once. */
	if (localdata && input_load_config("res/config/keybinds.json") == 0) {
		input_save_config(path);
		return;
	}

	get_legacy_config_path(path, sizeof(path));
	if (input_migrate_binary_config(path) == 0) {
		char json_path[MAX_PATH];
		get_shared_config_path(json_path, sizeof(json_path));
		input_save_config(json_path);
		return;
	}

	hotbar_setup_defaults();
	save_options();
}

/* The v3 staggered login often delivers SV_LOGINDONE before the skill
 * values, so the hotbar setup below cannot always run right away. When it
 * can't, this remembers what is still owed (1 = re-filter a loaded profile,
 * 2 = full fresh-character setup) and finish_character_options() settles
 * the debt once the values are in. */
static int char_options_pending;

void load_character_options(void)
{
	/* every SV_LOGINDONE means we just (re)entered the game world - make
	 * sure the chat window is not left scrolled up from the login flood */
	render_text_jump_bottom();

	/* The name comes from the login credentials, not from the map: with
	 * the protocol v3 staggered login the center tile often has no
	 * character yet when sv_logindone fires, and reading it here made
	 * the per-character profile load a coin flip. */
	if (username[0] == '\0') {
		return;
	}

	snprintf(active_charname, sizeof(active_charname), "%s", username);

	char path[MAX_PATH];
	get_config_path(path, sizeof(path));

	char shared[MAX_PATH];
	get_shared_config_path(shared, sizeof(shared));
	if (strcmp(path, shared) == 0) {
		return;
	}

	/* migrate a per-character profile out of the install dir (see load_options) */
	if (localdata) {
		char oldpath[MAX_PATH];
		snprintf(oldpath, sizeof(oldpath), "res/config/keybinds_%s.json", active_charname);
		FILE *probe = fopen(path, "r");
		if (probe) {
			fclose(probe);
		} else {
			FILE *oldfp = fopen(oldpath, "r");
			if (oldfp) {
				fclose(oldfp);
				if (input_load_config(oldpath) == 0) {
					input_save_config(path);
				}
			}
		}
	}

	if (input_load_config(path) == 0) {
		/* Every area transfer (including a death rescue) is a full
		 * relogin and reloads the profile so per-character binds follow
		 * the player across zones - but announcing it each time spammed
		 * the chat. Say it once per character. */
		static char announced_for[sizeof(active_charname)];
		if (strcmp(announced_for, active_charname) != 0) {
			snprintf(announced_for, sizeof(announced_for), "%s", active_charname);
			addline("Loaded keybinds for %s", active_charname);
		}
		/* saved profiles can still carry spells from before the skill
		 * filter existed (or from another class's shared config) */
		char_options_pending = (hotbar_filter_uncastable() < 0) ? 1 : 0;
		return;
	}

	/* No saved profile for this character: the hotbar currently holds the
	 * pre-login defaults, which include every spell in the game. Once
	 * skill values are known, strip what this character cannot cast and
	 * offer a recall scroll and a healing potion instead. */
	if (hotbar_filter_uncastable() >= 0) {
		hotbar_add_default_items();
		save_options();
		char_options_pending = 0;
	} else {
		char_options_pending = 2;
	}
}

void finish_character_options(void)
{
	if (!char_options_pending) {
		return;
	}
	if (hotbar_filter_uncastable() < 0) {
		return; /* skill values still on their way */
	}
	if (char_options_pending == 2) {
		hotbar_add_default_items();
		save_options();
	}
	char_options_pending = 0;
}

void init_logging(void)
{
	char filename[MAX_PATH];

	if ((game_options & GO_APPDATA) || (game_options & GO_NOTSET)) {
		localdata = SDL_GetPrefPath(ORG_NAME, APP_NAME);
		if (localdata) {
			snprintf(filename, sizeof(filename), "%s%s", localdata, "moac.log");
		} else {
			// Fallback if SDL_GetPrefPath fails
			snprintf(filename, sizeof(filename), "moac.log");
		}
	} else {
		snprintf(filename, sizeof(filename), "moac.log");
	}

	errorfp = fopen(filename, "a");
	if (!errorfp) {
		errorfp = stderr;
	}
}

void determine_resolution(void)
{
	/* When only one dimension is given, complete it to a 16:9 window: the
	 * widescreen canvas is the intended default. The old 4:3 mappings
	 * (800x600, 1600x1200, ...) are gone - anyone who wants those passes
	 * both -w and -h explicitly, which is left untouched. With neither
	 * given the client sizes itself to the monitor (see sdl_init()). */
	if (!want_height && want_width) {
		want_height = want_width * 9 / 16;
	}
	if (!want_width && want_height) {
		want_width = want_height * 16 / 9;
	}
}

static void set_v35_values(void)
{
	target_port = 27584;
	set_v35_inventory();
	set_v35_keytab();
	set_v35_actions();
	set_v35_skilltab();
}

// main
int main(int argc, char *argv[])
{
#if USE_MIMALLOC
	// Configure SDL to use mimalloc for all its internal allocations
	// This MUST be called before any SDL function, including SDL_GetPrefPath()
	if (!SDL_SetMemoryFunctions(MALLOC, CALLOC, REALLOC, FREE)) {
		// If this fails we should still carry on and just use malloc, but log error.
		SDL_Log("Failed to set memory functions for mimalloc: %s", SDL_GetError());
	}
#endif

	int ret;
	char buf[80];

	if ((ret = parse_args(argc, argv)) != 0) {
		return -1;
	}

	if (sv_ver == 35) {
		set_v35_values();
	}

	init_logging();

#ifdef ENABLE_CRASH_HANDLER
	register_crash_handler();
#endif

	teleport_init();
	amod_init();
	sprite_config_init();
	amod_sprite_config();
#ifdef USE_LUAJIT
	lua_scripting_init();
	lua_scripting_set_dev_mode(dev_mode != 0);
#endif
#ifdef ENABLE_SHAREDMEM
	sharedmem_init();
#endif

	/* remember the launcher-provided options before the saved config gets
	 * a chance to override them (see game_options_record_override) */
	game_options_note_launch();

	/* an explicit -o GO_GPU bit is a launch-time request that must survive
	 * the saved extra option (which load_options may set to off). The same
	 * goes for the shader-effects sub-flag - without this, a saved
	 * gpu_shader_effects:false silently swallowed an explicit -o bit, so
	 * the sprite batch could not be turned on from the command line at
	 * all (it did not even log that it had been asked for). */
	int launch_gpu = (!(game_options & GO_NOTSET) && (game_options & GO_GPU)) ? 1 : 0;
	int launch_shaderfx = (!(game_options & GO_NOTSET) && (game_options & GO_SHADERFX)) ? 1 : 0;

	load_options();

	// set some stuff
	if (!*username || !*password || !*server_url) {
		display_usage();
		return 0;
	}

	xlog(errorfp, "Client started with -h%d -w%d -o%" PRIu64, want_height, want_width, game_options);

#ifdef _WIN32
	SetProcessDPIAware();
#endif

	target_server = server_url;

	if (server_port) {
		if (server_port < 0 || server_port > UINT16_MAX) {
			target_port = 0;
		} else {
			target_port = (uint16_t)server_port;
		}
	}

	// init random
	rrandomize();

	determine_resolution();

	sprintf(buf, "Astonia 3 v%d.%d.%d", (VERSION >> 16) & 255, (VERSION >> 8) & 255, (VERSION) & 255);
	/* SDL_GPU renderer (experimental, opt-in, default OFF): decide BEFORE
	 * sdl_init creates the renderer. Requested via the saved extra option or
	 * the GO_GPU -o bit; when neither is set, no GPU code runs at all.
	 * sdl_init falls back to SDL_Renderer when the GPU path is requested but
	 * not usable (no device, missing shader pipelines). */
	/* DEFAULT ON: only an explicit saved false (0) turns it off. -1 means the
	 * key is absent, which is a fresh config and gets the default. */
	gpu_rendering_requested = launch_gpu || saved_gpu_rendering != 0 || ((game_options & GO_GPU) != 0);
	/* shader-effects sub-flag: only meaningful when the GPU renderer comes
	 * up; sdl_init ignores it otherwise */
	/* DEFAULT ON too, and still only meaningful under the GPU renderer */
	gpu_shaderfx_requested =
	    gpu_rendering_requested && (launch_shaderfx || saved_gpu_shaderfx != 0 || ((game_options & GO_SHADERFX) != 0));

	if (!sdl_init(want_width, want_height, buf, want_monitor)) {
		render_exit();
		return -1;
	}

	/* TTF text (experimental): needs sdl_scale, which is fixed by sdl_init.
	 * Re-apply the saved toggle first - sdl_init resets game_options to
	 * GO_DEFAULTS when the launcher passed no -o. fm_init is harmless when
	 * SDL3_ttf or the bundled fonts are missing - the option then simply
	 * stays without effect. */
	if (saved_ttf_text > 0) {
		game_options |= GO_TTF;
	} else if (saved_ttf_text == 0) {
		game_options &= ~GO_TTF;
	}
	fm_init();
	fm_set_enabled((game_options & GO_TTF) != 0);

	/* re-apply the GPU renderer preference for the same GO_DEFAULTS reason -
	 * the bit reflects the user's saved choice (shown in Options), while
	 * use_gpu_rendering reflects what sdl_init actually managed to set up */
	if (gpu_rendering_requested) {
		game_options |= GO_GPU;
	} else {
		game_options &= ~GO_GPU;
	}
	if (gpu_shaderfx_requested) {
		game_options |= GO_SHADERFX;
	} else {
		game_options &= ~GO_SHADERFX;
	}
	if (saved_gpu_fancyfx == 0) {
		game_options |= GO_NOFANCYFX;
	} else if (saved_gpu_fancyfx > 0) {
		game_options &= ~GO_NOFANCYFX;
	}

	render_init();
	loading_step(LS_SOUND);
	loading_present();
	init_sound();

	if (game_options & GO_LARGE) {
		namesize = 0;
		render_set_textfont(1);
	}

	main_init();
	help_init();
	update_user_keys();

	/* settings sdl_init cannot honor itself: it forces vsync on and creates
	 * the window from the launcher geometry */
	if (saved_vsync >= 0 && saved_vsync != sdl_vsync) {
		sdl_set_vsync(saved_vsync);
	}
	if (saved_window_mode >= 0) {
		options_apply_window_mode(saved_window_mode);
	}

	loading_step(LS_MODS);
	loading_present();
	main_loop();

#ifdef ENABLE_SHAREDMEM
	sharedmem_exit();
#endif
#ifdef USE_LUAJIT
	lua_scripting_exit();
#endif
	amod_exit();
	main_exit();
	sound_exit();
	render_exit();
	fm_cleanup();
	sdl_exit();

	list_mem();

	if (panic_reached) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "recursion panic", panic_reached_str, NULL);
	}
	if (xmemcheck_failed) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "memory panic", memcheck_failed_str, NULL);
	}

	if (localdata) {
		SDL_free(localdata);
	}

	xlog(errorfp, "Clean client shutdown. Thank you for playing!");
	if (errorfp != stderr) {
		fclose(errorfp);
	}
	return 0;
}
