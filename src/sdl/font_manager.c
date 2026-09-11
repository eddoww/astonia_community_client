/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * TTF Font Manager - see font_manager.h.
 *
 * Ported from the archived Ugaris_Client feature/newnormal_use_ttf branch and
 * reworked: SDL3_ttf instead of SDL2_ttf, three distinct sizes (small /
 * normal / big at pt * sdl_scale) instead of one fixed 16pt font, terminator-
 * aware measurement, and a cache generation for the text texture cache.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "dll.h"
#include "astonia.h"
#include "sdl/sdl.h"
#include "sdl/sdl_private.h"
#include "sdl/font_manager.h"

/* Mirrors of the text flag bits font selection cares about (values are fixed
 * API, see game.h RENDER_TEXT_* and game_private.h RENDER__*_FONT). */
#define FM_TEXT_SMALL    8
#define FM_TEXT_BIG      32
#define FM_UNDERLAY_FONT (128 | 256) /* RENDER__SHADED_FONT | RENDER__FRAMED_FONT */

#define FM_SIZE_SMALL  0
#define FM_SIZE_NORMAL 1
#define FM_SIZE_BIG    2
#define FM_SIZE_COUNT  3

/* Point sizes in game pixels; loaded at pt * sdl_scale so glyphs stay sharp
 * at every window scale. Chosen to fit the bitmap fonts' line grid (fontb ~8,
 * fonta ~10, fontc ~12 pixels of line spacing). */
#define FM_PT_SMALL  7
#define FM_PT_NORMAL 9
#define FM_PT_BIG    11

#define FM_MAX_FONTS     64
#define FM_MAX_FONT_NAME 64
#define FM_MAX_FONT_PATH 512
#define FM_FONT_DIR      "res/fonts"
#define FM_DEFAULT_FACE  "mplus-1m-bold"

/* Longest text sdl_maketext is ever asked for is a chat line (<256). */
#define FM_MAX_TEXT 1024

static int fm_is_enabled;
/* Text size in percent of the base point sizes (see fm_set_text_scale) and
 * the saved face choice - kept outside HAVE_SDL3_TTF so a build without
 * SDL3_ttf still carries the user's settings through save/load unchanged. */
static int fm_text_scale_pct = 100;
static char fm_pref_name[FM_MAX_FONT_NAME];

static int fm_clamp_scale(int pct)
{
	if (pct < FM_SCALE_MIN) {
		return FM_SCALE_MIN;
	}
	if (pct > FM_SCALE_MAX) {
		return FM_SCALE_MAX;
	}
	return pct;
}

#ifdef HAVE_SDL3_TTF

#include <SDL3_ttf/SDL_ttf.h>

struct fm_face {
	char name[FM_MAX_FONT_NAME];
	char path[FM_MAX_FONT_PATH];
};

static struct fm_face fm_faces[FM_MAX_FONTS];
static int fm_face_count;
static int fm_current = -1;

/* Two handles per size: plain and outline (outline width sdl_scale, for
 * the shaded/framed underlay pass). SDL_ttf flushes a font's whole glyph
 * cache on every TTF_SetFontOutline() call, so the old single-handle
 * toggle re-rasterized every glyph through FreeType - with per-glyph
 * FILE reads - on every underlay string build. Dedicated handles keep
 * both caches warm forever. The faces are opened from ONE shared memory
 * buffer (fm_face_mem) so FreeType never touches the disk again after
 * load. tests/test_text_compare.c part C proves the rendered output is
 * bit-identical to the old toggle approach. */
static TTF_Font *fm_font[FM_SIZE_COUNT];
static TTF_Font *fm_font_outline[FM_SIZE_COUNT];
static void *fm_face_mem; /* backing buffer of the open faces */
static int fm_adv[FM_SIZE_COUNT][128]; /* per-character advance, device pixels */
static int fm_lineskip[FM_SIZE_COUNT]; /* line skip, device pixels */

static int fm_initialized;
static uint32_t fm_generation;

static int fm_size_class(int flags)
{
	if (flags & FM_TEXT_SMALL) {
		return FM_SIZE_SMALL;
	}
	if (flags & FM_TEXT_BIG) {
		return FM_SIZE_BIG;
	}
	return FM_SIZE_NORMAL;
}

static int fm_base_pt(int size_class)
{
	switch (size_class) {
	case FM_SIZE_SMALL:
		return FM_PT_SMALL;
	case FM_SIZE_BIG:
		return FM_PT_BIG;
	default:
		return FM_PT_NORMAL;
	}
}

/* Device point size: base pt, scaled by the Text Size percent and the
 * window scale. Fractional sizes are deliberate (7pt at 85% is 5.95pt,
 * not 5pt) - FreeType hints them onto the pixel grid just fine. */
static float fm_pt_for_size(int size_class)
{
	return (float)(fm_base_pt(size_class) * fm_text_scale_pct * sdl_scale) / 100.0f;
}

static void fm_close_fonts(void)
{
	int s;

	for (s = 0; s < FM_SIZE_COUNT; s++) {
		if (fm_font[s]) {
			TTF_CloseFont(fm_font[s]);
			fm_font[s] = NULL;
		}
		if (fm_font_outline[s]) {
			TTF_CloseFont(fm_font_outline[s]);
			fm_font_outline[s] = NULL;
		}
	}
	/* safe only after every face handle is closed (FreeType reads the
	 * memory face on demand) */
	if (fm_face_mem) {
		SDL_free(fm_face_mem);
		fm_face_mem = NULL;
	}
}

/* Open one face handle from the shared memory buffer. */
static TTF_Font *fm_open_from_mem(const void *mem, size_t size, float pt)
{
	SDL_IOStream *io = SDL_IOFromConstMem(mem, size);

	if (!io) {
		return NULL;
	}
	/* closeio=true: the stream is owned by the font (the buffer is not) */
	return TTF_OpenFontIO(io, true, pt);
}

/* Copy printable ASCII from text into buf, stopping at NUL, the draw text
 * terminator, n characters (n < 0: no limit) or a full buffer. The bitmap
 * renderer skips characters outside its raster the same way; staying pure
 * ASCII also guarantees valid UTF-8 for SDL_ttf. Returns the length. */
static int fm_sanitize(const char *text, int n, char *buf, int bufsize)
{
	int len = 0;
	const unsigned char *c = (const unsigned char *)text;

	for (; *c && len < bufsize - 1; c++) {
		if (*c == (unsigned char)RENDER_TEXT_TERMINATOR) {
			break;
		}
		if (n >= 0 && n-- == 0) {
			break;
		}
		if (*c < 32 || *c > 126) {
			continue;
		}
		buf[len++] = (char)*c;
	}
	buf[len] = 0;
	return len;
}

static int fm_load_face(int idx)
{
	int s, c;
	size_t mem_size = 0;

	if (idx < 0 || idx >= fm_face_count) {
		return 0;
	}

	fm_close_fonts();

	/* one read of the font file; every face handle serves from memory */
	fm_face_mem = SDL_LoadFile(fm_faces[idx].path, &mem_size);
	if (!fm_face_mem) {
		warn("font_manager: failed to read %s: %s", fm_faces[idx].path, SDL_GetError());
		return 0;
	}

	for (s = 0; s < FM_SIZE_COUNT; s++) {
		float pt = fm_pt_for_size(s);

		fm_font[s] = fm_open_from_mem(fm_face_mem, mem_size, pt);
		fm_font_outline[s] = fm_open_from_mem(fm_face_mem, mem_size, pt);
		if (!fm_font[s] || !fm_font_outline[s]) {
			warn("font_manager: failed to load %s at %.1fpt: %s", fm_faces[idx].path, (double)pt, SDL_GetError());
			fm_close_fonts();
			/* no face is loaded now: fm_available() must say so, or the
			 * renderer dereferences the NULL handles */
			fm_current = -1;
			return 0;
		}
		/* mono hinting keeps small sizes crisp on the pixel grid */
		TTF_SetFontHinting(fm_font[s], TTF_HINTING_MONO);
		TTF_SetFontHinting(fm_font_outline[s], TTF_HINTING_MONO);
		/* underlay pass: grow the glyphs by one game pixel in every
		 * direction - set ONCE; toggling the outline flushes SDL_ttf's
		 * glyph cache (that was the per-frame FreeType file I/O) */
		TTF_SetFontOutline(fm_font_outline[s], sdl_scale);

		fm_lineskip[s] = TTF_GetFontLineSkip(fm_font[s]);

		for (c = 0; c < 128; c++) {
			fm_adv[s][c] = 0;
		}
		for (c = 32; c < 127; c++) {
			char one[2];
			int w = 0, h = 0;

			one[0] = (char)c;
			one[1] = 0;
			if (TTF_GetStringSize(fm_font[s], one, 0, &w, &h)) {
				fm_adv[s][c] = w;
			}
		}
	}

	fm_current = idx;
	fm_generation++;
	note("font_manager: using \"%s\" (%d/%d/%dpt at %d%%, scale %d)", fm_faces[idx].name, FM_PT_SMALL, FM_PT_NORMAL,
	    FM_PT_BIG, fm_text_scale_pct, sdl_scale);
	return 1;
}

static int fm_find_face(const char *name)
{
	int i;

	if (!name || !name[0]) {
		return -1;
	}
	for (i = 0; i < fm_face_count; i++) {
		if (!strcmp(fm_faces[i].name, name)) {
			return i;
		}
	}
	return -1;
}

static int fm_face_cmp(const void *a, const void *b)
{
	return strcmp(((const struct fm_face *)a)->name, ((const struct fm_face *)b)->name);
}

static void fm_scan_pattern(const char *pattern)
{
	int count = 0, i;
	char **list = SDL_GlobDirectory(FM_FONT_DIR, pattern, SDL_GLOB_CASEINSENSITIVE, &count);

	if (!list) {
		return;
	}
	for (i = 0; i < count && fm_face_count < FM_MAX_FONTS; i++) {
		struct fm_face *f = &fm_faces[fm_face_count];
		char *dot;

		snprintf(f->path, sizeof(f->path), "%s/%s", FM_FONT_DIR, list[i]);
		snprintf(f->name, sizeof(f->name), "%s", list[i]);
		dot = strrchr(f->name, '.');
		if (dot) {
			*dot = 0;
		}
		fm_face_count++;
	}
	SDL_free(list);
}

int fm_init(void)
{
	int idx = 0, i;

	if (fm_initialized) {
		return fm_available();
	}

	if (!TTF_Init()) {
		warn("font_manager: TTF_Init failed: %s", SDL_GetError());
		return 0;
	}
	fm_initialized = 1;

	fm_scan_pattern("*.ttf");
	fm_scan_pattern("*.otf");
	if (fm_face_count == 0) {
		note("font_manager: no fonts found in %s", FM_FONT_DIR);
		return 0;
	}
	SDL_qsort(fm_faces, (size_t)fm_face_count, sizeof(fm_faces[0]), fm_face_cmp);

	/* the saved choice first, then the bundled default, then anything */
	idx = fm_find_face(fm_pref_name);
	if (idx < 0) {
		idx = fm_find_face(FM_DEFAULT_FACE);
	}
	if (idx < 0) {
		idx = 0;
	}
	if (fm_load_face(idx)) {
		return 1;
	}
	/* default face broken - try the others */
	for (i = 0; i < fm_face_count; i++) {
		if (i != idx && fm_load_face(i)) {
			return 1;
		}
	}
	return 0;
}

void fm_cleanup(void)
{
	if (!fm_initialized) {
		return;
	}
	fm_close_fonts();
	TTF_Quit();
	fm_initialized = 0;
	fm_current = -1;
}

int fm_available(void)
{
	return fm_initialized && fm_current >= 0;
}

uint32_t fm_cache_generation(void)
{
	return fm_active() ? fm_generation : 0;
}

int fm_font_count(void)
{
	return fm_face_count;
}

const char *fm_font_name(int idx)
{
	if (idx < 0 || idx >= fm_face_count) {
		return NULL;
	}
	return fm_faces[idx].name;
}

const char *fm_current_font_name(void)
{
	if (fm_current < 0) {
		return NULL;
	}
	return fm_faces[fm_current].name;
}

int fm_current_font_index(void)
{
	return fm_current;
}

int fm_select_font(const char *name)
{
	int idx = fm_find_face(name);
	int prev = fm_current;

	if (idx < 0) {
		return 0;
	}
	if (fm_load_face(idx)) {
		snprintf(fm_pref_name, sizeof(fm_pref_name), "%s", name);
		return 1;
	}
	/* a broken file must not leave the client without a face mid-session:
	 * go back to the one that worked */
	if (prev >= 0 && prev != idx) {
		fm_load_face(prev);
	}
	return 0;
}

void fm_set_preferred_font(const char *name)
{
	snprintf(fm_pref_name, sizeof(fm_pref_name), "%s", name ? name : "");
	if (fm_initialized && fm_pref_name[0] && fm_find_face(fm_pref_name) != fm_current) {
		fm_select_font(fm_pref_name);
	}
}

int fm_text_scale(void)
{
	return fm_text_scale_pct;
}

int fm_set_text_scale(int pct)
{
	pct = fm_clamp_scale(pct);
	if (pct == fm_text_scale_pct) {
		return fm_current >= 0;
	}
	fm_text_scale_pct = pct;
	if (fm_current < 0) {
		return 0; /* applied when a face gets loaded */
	}
	/* same face, new sizes: fm_load_face bumps the cache generation, so
	 * every cached text texture is rebuilt at the new size on next use */
	return fm_load_face(fm_current);
}

int fm_text_width(int flags, const char *text, int n)
{
	char buf[FM_MAX_TEXT];
	int s, len, i, w = 0;

	if (!fm_available() || !text) {
		return 0;
	}
	s = fm_size_class(flags);
	len = fm_sanitize(text, n, buf, sizeof(buf));
	for (i = 0; i < len; i++) {
		w += fm_adv[s][(unsigned char)buf[i]];
	}
	return (w + sdl_scale - 1) / sdl_scale;
}

int fm_char_width(int flags, unsigned char c)
{
	if (!fm_available() || c > 127) {
		return 0;
	}
	return (fm_adv[fm_size_class(flags)][c] + sdl_scale - 1) / sdl_scale;
}

int fm_line_height(int flags)
{
	if (!fm_available()) {
		return 10;
	}
	return (fm_lineskip[fm_size_class(flags)] + sdl_scale - 1) / sdl_scale;
}

int fm_space_width(int flags)
{
	return fm_char_width(flags, ' ');
}

SDL_Surface *fm_render_text_surface(const char *text, uint32_t color, int flags)
{
	char buf[FM_MAX_TEXT];
	TTF_Font *font;
	SDL_Surface *surface;
	SDL_Color col;

	if (!fm_available() || !text) {
		return NULL;
	}
	if (!fm_sanitize(text, -1, buf, sizeof(buf))) {
		return NULL;
	}

	/* Shaded/framed underlay pass renders through the dedicated outline
	 * handle (glyphs grown by one game pixel in every direction;
	 * render_text_alpha() draws this pass at -1/-1, so the outline
	 * surrounds the main pass exactly). Dedicated handles keep both
	 * glyph caches warm - see the note at fm_font_outline. */
	if (flags & FM_UNDERLAY_FONT) {
		font = fm_font_outline[fm_size_class(flags)];
	} else {
		font = fm_font[fm_size_class(flags)];
	}

	col.r = (Uint8)((color >> 16) & 0xFF);
	col.g = (Uint8)((color >> 8) & 0xFF);
	col.b = (Uint8)(color & 0xFF);
	col.a = (Uint8)((color >> 24) & 0xFF);

	surface = TTF_RenderText_Blended(font, buf, 0, col);

	if (!surface) {
		warn("font_manager: render failed: %s", SDL_GetError());
	}
	return surface;
}

#else /* !HAVE_SDL3_TTF - stubs so callers need no #ifdefs */

int fm_init(void)
{
	return 0;
}

void fm_cleanup(void) {}

int fm_available(void)
{
	return 0;
}

uint32_t fm_cache_generation(void)
{
	return 0;
}

int fm_font_count(void)
{
	return 0;
}

const char *fm_font_name(int idx)
{
	(void)idx;
	return NULL;
}

const char *fm_current_font_name(void)
{
	return NULL;
}

int fm_current_font_index(void)
{
	return -1;
}

int fm_select_font(const char *name)
{
	(void)name;
	return 0;
}

void fm_set_preferred_font(const char *name)
{
	/* remembered so the saved choice survives a round trip through a
	 * build without SDL3_ttf */
	snprintf(fm_pref_name, sizeof(fm_pref_name), "%s", name ? name : "");
}

int fm_text_scale(void)
{
	return fm_text_scale_pct;
}

int fm_set_text_scale(int pct)
{
	fm_text_scale_pct = fm_clamp_scale(pct);
	return 0;
}

int fm_text_width(int flags, const char *text, int n)
{
	(void)flags;
	(void)text;
	(void)n;
	return 0;
}

int fm_char_width(int flags, unsigned char c)
{
	(void)flags;
	(void)c;
	return 0;
}

int fm_line_height(int flags)
{
	(void)flags;
	return 10;
}

int fm_space_width(int flags)
{
	(void)flags;
	return 4;
}

SDL_Surface *fm_render_text_surface(const char *text, uint32_t color, int flags)
{
	(void)text;
	(void)color;
	(void)flags;
	return NULL;
}

#endif /* HAVE_SDL3_TTF */

void fm_set_enabled(int on)
{
	fm_is_enabled = on ? 1 : 0;
}

int fm_enabled(void)
{
	return fm_is_enabled;
}

int fm_active(void)
{
	return fm_is_enabled && fm_available();
}
