/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Bitmap-font glyph rasterizer (GPU-free layer).
 *
 * Rasterizes ONE character of a classic run-length bitmap font
 * (struct renderfont) into a standalone coverage mask, replaying the
 * exact same RLE walk sdl_rendertext_to_pixels() uses for whole
 * strings. The batched GPU text path (sdl_gpu_text.c) uploads these
 * masks into the shared texture atlas once per (font, character) and
 * then draws text as per-glyph instanced quads.
 *
 * Correctness contract: compositing the per-glyph masks at the string's
 * per-character advances must reproduce sdl_rendertext_to_pixels()
 * bit-for-bit for any single color at full opacity (all inked pixels
 * are the same opaque color, so overlapping glyphs - e.g. the derived
 * _shaded/_framed fonts whose ink extends past the advance - write
 * identical values in both orders). tests/test_text_compare.c verifies
 * this against the real string rasterizer.
 *
 * This file must stay free of GPU/renderer dependencies: the unit tests
 * compile it standalone.
 */

#ifndef SDL_TEXT_GLYPH_H
#define SDL_TEXT_GLYPH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct renderfont;

/* Glyph ink is bounded by the 64x64 letter buffer every font variant is
 * generated through (render.c render_create_rawrun / MAXFONTHEIGHT). */
#define TEXT_GLYPH_MAX_DIM 64

/* One rasterized glyph mask, anchored at the character cell origin
 * (pen position): pixel (0,0) of the mask is the cell origin, so a
 * quad drawn at the pen position with size w x h reproduces the
 * string rasterizer's placement. All units are device pixels (the
 * font raw data is authored per sdl_scale). */
typedef struct text_glyph_mask {
	int w, h; /* ink bounding box from the cell origin; 0 x 0 = empty glyph */
	int advance; /* pen advance in device pixels (font dim * scale) */
	/* w*h ARGB pixels: 0xFFFFFFFF where inked, 0 elsewhere. Only valid
	 * when w > 0 (points into the caller-provided buffer). */
	uint32_t *pixel;
} text_glyph_mask_t;

/* Rasterize character `c` of `font` at `scale`. `buffer` must hold
 * TEXT_GLYPH_MAX_DIM * TEXT_GLYPH_MAX_DIM uint32_t and backs
 * out->pixel. Returns 1 on success (including empty glyphs: w == 0),
 * 0 when the glyph cannot be rasterized (no raw data / bad char). */
int text_glyph_rasterize(
    const struct renderfont *font, unsigned char c, int scale, uint32_t *buffer, text_glyph_mask_t *out);

/* Clip one glyph quad at device-space pen position (pen_x, pen_y)
 * against the half-open device-space window [clip_x0, clip_x1) x
 * [clip_y0, clip_y1). On visible output writes the clipped dest origin,
 * the mask-relative source origin and the visible size; returns 0 when
 * the glyph is empty or fully clipped. Shared by the client's batched
 * text path and the comparison harness so their geometry cannot drift. */
int text_glyph_quad(const text_glyph_mask_t *m, int pen_x, int pen_y, int clip_x0, int clip_y0, int clip_x1,
    int clip_y1, int *dst_x, int *dst_y, int *src_x, int *src_y, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif /* SDL_TEXT_GLYPH_H */
