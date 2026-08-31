/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Bitmap-font glyph rasterizer - see sdl_text_glyph.h.
 */

#include <string.h>

#include "sdl/sdl_text_glyph.h"

/* mirror of struct renderfont (game_private.h / sdl_private.h) - kept
 * local so this file has no game/SDL header dependencies */
#ifndef HAVE_DDFONT
#define HAVE_DDFONT

struct renderfont {
	int dim;
	unsigned char *raw;
};
#endif

int text_glyph_rasterize(
    const struct renderfont *font, unsigned char c, int scale, uint32_t *buffer, text_glyph_mask_t *out)
{
	const unsigned char *rawrun;
	int x, y, max_x, max_y;

	memset(out, 0, sizeof(*out));

	if (!font || c > 127 || scale < 1) {
		return 0;
	}
	rawrun = font[c].raw;
	if (!rawrun) {
		return 0;
	}

	out->advance = font[c].dim * scale;

	memset(buffer, 0, (size_t)TEXT_GLYPH_MAX_DIM * TEXT_GLYPH_MAX_DIM * sizeof(uint32_t));

	/* Exact replay of the per-character inner loop of
	 * sdl_rendertext_to_pixels() (sdl_draw.c): each RLE entry skips
	 * `*rawrun` pixels and paints ONE pixel at the new position; 254
	 * advances to the next row, 255 terminates. The string rasterizer
	 * paints into an unbounded-width row at the running pen position;
	 * here the pen is the mask origin. Ink is bounded by the 64x64
	 * letter buffer all font variants are generated through, but clamp
	 * defensively anyway. */
	x = 0;
	y = 0;
	max_x = -1;
	max_y = -1;

	while (*rawrun != 255) {
		if (*rawrun == 254) {
			y++;
			x = 0;
			rawrun++;
			continue;
		}

		x += *rawrun;
		rawrun++;

		if (x < TEXT_GLYPH_MAX_DIM && y < TEXT_GLYPH_MAX_DIM) {
			buffer[x + y * TEXT_GLYPH_MAX_DIM] = 0xFFFFFFFFu;
			if (x > max_x) {
				max_x = x;
			}
			if (y > max_y) {
				max_y = y;
			}
		}
	}

	if (max_x < 0) {
		/* empty glyph (e.g. space): valid, draws nothing */
		out->w = 0;
		out->h = 0;
		out->pixel = NULL;
		return 1;
	}

	/* compact the ink box to its own tightly-packed w*h block at the
	 * start of the buffer (rows move forward only - no overlap issues
	 * as long as w <= TEXT_GLYPH_MAX_DIM, which always holds) */
	out->w = max_x + 1;
	out->h = max_y + 1;
	for (y = 0; y < out->h; y++) {
		memmove(buffer + (size_t)y * (size_t)out->w, buffer + (size_t)y * TEXT_GLYPH_MAX_DIM,
		    (size_t)out->w * sizeof(uint32_t));
	}
	out->pixel = buffer;
	return 1;
}

int text_glyph_quad(const text_glyph_mask_t *m, int pen_x, int pen_y, int clip_x0, int clip_y0, int clip_x1,
    int clip_y1, int *dst_x, int *dst_y, int *src_x, int *src_y, int *w, int *h)
{
	int x0, y0, x1, y1;

	if (m->w <= 0 || m->h <= 0) {
		return 0;
	}

	x0 = pen_x;
	y0 = pen_y;
	x1 = pen_x + m->w;
	y1 = pen_y + m->h;

	if (x0 < clip_x0) {
		x0 = clip_x0;
	}
	if (y0 < clip_y0) {
		y0 = clip_y0;
	}
	if (x1 > clip_x1) {
		x1 = clip_x1;
	}
	if (y1 > clip_y1) {
		y1 = clip_y1;
	}
	if (x0 >= x1 || y0 >= y1) {
		return 0;
	}

	*dst_x = x0;
	*dst_y = y0;
	*src_x = x0 - pen_x;
	*src_y = y0 - pen_y;
	*w = x1 - x0;
	*h = y1 - y0;
	return 1;
}
