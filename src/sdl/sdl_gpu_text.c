/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Batched GPU text - see sdl_gpu_text.h.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "astonia.h"
#include "sdl/sdl.h"
#include "sdl/sdl_private.h"
#include "sdl/sdl_gpu.h"
#include "sdl/sdl_gpu_draw.h"
#include "sdl/sdl_gpu_atlas.h"
#include "sdl/sdl_gpu_shaderfx.h"
#include "sdl/sdl_text_glyph.h"
#include "sdl/sdl_gpu_text.h"

/* The client uses nine RenderFont tables (fonta/b/c and their derived
 * _shaded/_framed variants); mods could add more. */
#define GT_MAX_FONTS 32

enum {
	GT_UNBUILT = 0,
	GT_READY, /* mask in the atlas */
	GT_EMPTY, /* no ink (space) */
	GT_FAILED, /* not representable - string falls back */
};

typedef struct gt_glyph {
	SDL_GPUTexture *tex; /* atlas page (GT_READY only) */
	const unsigned char *raw; /* font raw pointer at build time (staleness check:
	                           * render_create_font swaps the rasters at scale > 1) */
	uint16_t ax, ay; /* atlas origin */
	uint8_t w, h; /* ink box (device px, <= TEXT_GLYPH_MAX_DIM) */
	uint8_t state;
	int advance; /* pen advance in device px */
} gt_glyph_t;

typedef struct gt_font {
	struct renderfont *font;
	gt_glyph_t g[128];
} gt_font_t;

static struct {
	gt_font_t fonts[GT_MAX_FONTS];
	int num_fonts;
	int scale; /* sdl_scale the cache was built for */

	int stat_runs;
	int stat_glyphs;
	int stat_fallbacks;
} gt = {0};

static void gt_release_glyphs(void)
{
	for (int f = 0; f < gt.num_fonts; f++) {
		for (int c = 0; c < 128; c++) {
			gt_glyph_t *g = &gt.fonts[f].g[c];
			if (g->state == GT_READY && g->tex) {
				gpu_atlas_release(g->tex, g->ax, g->ay);
			}
			memset(g, 0, sizeof(*g));
		}
	}
	gt.num_fonts = 0;
	memset(gt.fonts, 0, sizeof(gt.fonts));
}

void gpu_text_reset(void)
{
	gt_release_glyphs();
	gt.scale = 0;
}

void gpu_text_frame_begin(void)
{
	gt.stat_runs = 0;
	gt.stat_glyphs = 0;
	gt.stat_fallbacks = 0;
}

void gpu_text_get_stats(int *runs, int *glyphs, int *fallbacks)
{
	if (runs) {
		*runs = gt.stat_runs;
	}
	if (glyphs) {
		*glyphs = gt.stat_glyphs;
	}
	if (fallbacks) {
		*fallbacks = gt.stat_fallbacks;
	}
}

static gt_font_t *gt_font_slot(struct renderfont *font)
{
	for (int f = 0; f < gt.num_fonts; f++) {
		if (gt.fonts[f].font == font) {
			return &gt.fonts[f];
		}
	}
	if (gt.num_fonts >= GT_MAX_FONTS) {
		return NULL;
	}
	gt_font_t *slot = &gt.fonts[gt.num_fonts++];
	slot->font = font;
	return slot;
}

/* Get (building if needed) the glyph entry for one character. Returns
 * NULL when the font table is full; entry state GT_FAILED means the
 * containing string cannot be batched. */
static gt_glyph_t *gt_glyph(gt_font_t *slot, unsigned char c)
{
	gt_glyph_t *g = &slot->g[c];
	const unsigned char *raw = slot->font[c].raw;

	/* revalidate: render_create_font replaces the raw rasters when the
	 * window scale is > 1 (PNG fonts), invalidating masks built from the
	 * embedded scale-1 data during early loading screens */
	if (g->state != GT_UNBUILT && g->raw == raw) {
		return g;
	}
	if (g->state == GT_READY && g->tex) {
		gpu_atlas_release(g->tex, g->ax, g->ay);
	}
	memset(g, 0, sizeof(*g));
	g->raw = raw;

	uint32_t buffer[TEXT_GLYPH_MAX_DIM * TEXT_GLYPH_MAX_DIM];
	text_glyph_mask_t m;

	if (!text_glyph_rasterize(slot->font, c, sdl_scale, buffer, &m)) {
		g->state = GT_FAILED;
		return g;
	}
	g->advance = m.advance;
	if (m.w == 0) {
		g->state = GT_EMPTY;
		return g;
	}

	int ax = 0, ay = 0;
	SDL_GPUTexture *tex = gpu_atlas_insert(m.pixel, m.w, m.h, &ax, &ay);
	if (!tex) {
		/* atlas full: try again next time (the string falls back for now) */
		g->state = GT_UNBUILT;
		g->raw = NULL;
		return NULL;
	}
	g->tex = tex;
	g->ax = (uint16_t)ax;
	g->ay = (uint16_t)ay;
	g->w = (uint8_t)m.w;
	g->h = (uint8_t)m.h;
	g->state = GT_READY;
	return g;
}

int gpu_text_draw_run(const char *text, struct renderfont *font, int r, int g, int b, int sx, int sy, int clipsx,
    int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	const char *c;
	int n = 0;

	if (!gpu_shaderfx_ready() || !font) {
		return 0;
	}
	/* the plain-mode pipeline is standard BLEND; anything else must use
	 * the direct draw with its per-mode pipeline */
	if (gpu_draw_get_blend_mode() != 0) {
		gt.stat_fallbacks++;
		return 0;
	}

	if (gt.scale != sdl_scale) {
		/* window scale changed (or first use): masks are per-scale */
		gt_release_glyphs();
		gt.scale = sdl_scale;
	}

	gt_font_t *slot = gt_font_slot(font);
	if (!slot) {
		gt.stat_fallbacks++;
		return 0;
	}

	/* pass 1: make sure every glyph of the run is representable and
	 * resident BEFORE emitting anything - a batched string is all or
	 * nothing (never draw a partial string and then fall back) */
	for (c = text; *c && *c != RENDER_TEXT_TERMINATOR; c++) {
		if (*c < 0) {
			continue; /* same skip as the string rasterizer */
		}
		gt_glyph_t *gl = gt_glyph(slot, (unsigned char)*c);
		if (!gl || gl->state == GT_FAILED) {
			gt.stat_fallbacks++;
			return 0;
		}
		if (gl->state == GT_READY) {
			n++;
		}
	}
	if (n > gpu_shaderfx_capacity()) {
		gt.stat_fallbacks++;
		return 0;
	}
	if (n == 0) {
		return 1; /* nothing visible (spaces only) - handled */
	}

	/* device-space clip window (the string path clips in logical pixels
	 * and scales; both cuts land on the same device boundaries, see
	 * tests/test_text_compare.c part B "clipped") */
	int cx0 = (clipsx + x_offset) * sdl_scale;
	int cy0 = (clipsy + y_offset) * sdl_scale;
	int cx1 = (clipex + x_offset) * sdl_scale;
	int cy1 = (clipey + y_offset) * sdl_scale;

	int pen_x = (sx + x_offset) * sdl_scale;
	int pen_y = (sy + y_offset) * sdl_scale;

	/* pass 2: emit */
	for (c = text; *c && *c != RENDER_TEXT_TERMINATOR; c++) {
		if (*c < 0) {
			continue;
		}
		gt_glyph_t *gl = &slot->g[(unsigned char)*c];
		if (gl->state == GT_READY) {
			text_glyph_mask_t m = {.w = gl->w, .h = gl->h, .advance = gl->advance};
			int dst_x, dst_y, src_x, src_y, w, h;

			if (text_glyph_quad(&m, pen_x, pen_y, cx0, cy0, cx1, cy1, &dst_x, &dst_y, &src_x, &src_y, &w, &h)) {
				if (!gpu_shaderfx_plain_quad(gl->tex, (float)dst_x, (float)dst_y, (float)w, (float)h, gl->ax + src_x,
				        gl->ay + src_y, w, h, r, g, b, 255)) {
					/* capacity was pre-checked; only a mid-frame shutdown
					 * gets here - the glyphs already emitted draw the same
					 * opaque pixels the fallback string will, so this stays
					 * visually exact */
					gt.stat_fallbacks++;
					return 0;
				}
				gt.stat_glyphs++;
			}
		}
		pen_x += gl->advance;
	}

	gt.stat_runs++;
	return 1;
}
