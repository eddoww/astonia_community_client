/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Batched GPU text (glyph atlas + instanced quads).
 *
 * Bitmap-font strings are drawn as one GPU_FX_MODE_PLAIN instance per
 * glyph through the sprite_fx batcher: glyph coverage masks are
 * rasterized ONCE per (font, character) by sdl_text_glyph.c and packed
 * into the shared texture atlas, so text stops creating per-string
 * textures, stops walking the text cache hash, and joins the same
 * instanced draw runs as everything else.
 *
 * Pixel-exactness contract: for opaque draws the per-glyph composite
 * is bit-identical to the whole-string texture (all inked pixels of a
 * pass share one opaque color, so even the overlapping ink of the
 * derived _shaded/_framed fonts writes identical values in any order).
 * tests/test_text_compare.c enforces this against the real string
 * rasterizer and through the real pipelines. Draws with alpha < 255
 * are NOT eligible (overlapping ink would double-blend) - they stay on
 * the cached-string path, which the caller batches as a single plain
 * quad instead.
 */

#ifndef SDL_GPU_TEXT_H
#define SDL_GPU_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

struct renderfont;

/* Draw one string as batched per-glyph quads. (sx, sy) is the ALIGNED
 * draw position in logical coordinates (same clip/offset semantics as
 * sdl_drawtext); r/g/b is the text color 0..255. Returns 1 when the
 * whole string was batched (or fully clipped), 0 when the caller must
 * fall back to the cached-string path (path inactive, TTF mode, glyph
 * rasterization failed, atlas or instance budget exhausted). Never
 * emits a partial string. */
int gpu_text_draw_run(const char *text, struct renderfont *font, int r, int g, int b, int sx, int sy, int clipsx,
    int clipsy, int clipex, int clipey, int x_offset, int y_offset);

/* Per-frame stats reset; called from gpu_frame_begin. */
void gpu_text_frame_begin(void);

/* Batched-text stats for the current frame (runs = strings batched,
 * glyphs = quads emitted, fallbacks = strings refused). */
void gpu_text_get_stats(int *runs, int *glyphs, int *fallbacks);

/* Forget every cached glyph (client shutdown; also safe on scale
 * changes - the cache revalidates glyphs against the font's raw
 * pointers and the global scale by itself). */
void gpu_text_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_GPU_TEXT_H */
