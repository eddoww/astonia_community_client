/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * TTF Font Manager
 *
 * Optional SDL3_ttf-backed text rendering. Scans res/fonts/ for .ttf/.otf
 * files, keeps the current face loaded at three pixel sizes (small / normal /
 * big, multiplied by sdl_scale) and hands out rendered text surfaces plus
 * metrics in game-pixel units.
 *
 * Everything degrades gracefully: when the client is built without SDL3_ttf
 * (no HAVE_SDL3_TTF), when no font could be loaded, or when the user toggle
 * is off, fm_active() returns 0 and the classic bitmap font path is used
 * unchanged. Callers only need to branch on fm_active().
 *
 * All calls are render-thread only (the text texture path already is).
 */

#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include <stdint.h>
#include <SDL3/SDL.h>

/* Ported from the archived Ugaris_Client feature/newnormal_use_ttf branch
 * (src/gui/font_manager.{c,h}), reworked for SDL3_ttf, three distinct font
 * sizes and cache-key support. */

/* Initialize SDL_ttf, scan res/fonts/ and load the default face.
 * Returns 1 when at least one face is usable, 0 otherwise (harmless). */
int fm_init(void);
void fm_cleanup(void);

/* User toggle (follows the GO_TTF game option). Enabling/disabling bumps the
 * cache generation so cached bitmap/TTF text textures never mix. */
void fm_set_enabled(int on);
int fm_enabled(void);

/* Compiled in + initialized + a face is loaded. */
int fm_available(void);

/* fm_available() && fm_enabled(): the TTF path should be used. */
int fm_active(void);

/* Cache-key component for text textures: 0 while the bitmap path is active,
 * otherwise a monotonically increasing value that changes whenever the face,
 * the sizes or the enabled state change. Together with the text flags (which
 * carry small/big) this uniquely identifies how a text texture was built. */
uint32_t fm_cache_generation(void);

/* Font registry (Options > Display > Font cycles through it). */
int fm_font_count(void);
const char *fm_font_name(int idx);
const char *fm_current_font_name(void);
int fm_current_font_index(void); /* -1 while no face is loaded */
int fm_select_font(const char *name); /* reloads all three sizes, bumps generation */
/* The saved face choice: applied at fm_init instead of the built-in default
 * (unknown names fall back to it), or right away when already initialized.
 * NULL/"" clears the preference. */
void fm_set_preferred_font(const char *name);

/* Text size (Options > Display > Text Size), percent of the base point
 * sizes 7/9/11pt. Scales the TTF path only - the bitmap fonts have their
 * own "Large Font" option. Clamped to FM_SCALE_MIN..FM_SCALE_MAX: below
 * 80% the small face drops under 6px and stops being readable; above 115%
 * the small face's mono-hinted advance rounds up to the next pixel and the
 * longest skill rows ("Negative experience 983", "Hitpoints 200+18/200/200")
 * run into their values - every window keeps its bitmap-era widths, so the
 * text must fit them, not the other way round. A change reloads the face
 * live and bumps the cache generation. */
#define FM_SCALE_MIN  80
#define FM_SCALE_MAX  115
#define FM_SCALE_STEP 5
int fm_text_scale(void);
int fm_set_text_scale(int pct); /* returns 1 when a face is loaded at the new size */

/* Metrics, all in game pixels (already divided by sdl_scale).
 * Width functions stop at RENDER_TEXT_TERMINATOR and at n characters
 * (n < 0: no limit), mirroring the bitmap measurement semantics. */
int fm_text_width(int flags, const char *text, int n);
int fm_char_width(int flags, unsigned char c);
int fm_line_height(int flags);
int fm_space_width(int flags);

/* Render text (up to the terminator) into a fresh ARGB surface at device
 * resolution (sdl_scale times the game-pixel size). color is 0xAARRGGBB.
 * When flags carries a shaded/framed underlay bit the glyphs are rendered
 * with a one-game-pixel outline (the underlay is drawn offset by -1/-1, so
 * the outline lines up around the main pass). Returns NULL on failure -
 * callers fall back to the bitmap raster. */
SDL_Surface *fm_render_text_surface(const char *text, uint32_t color, int flags);

#endif /* FONT_MANAGER_H */
