/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL - Drawing Module
 *
 * Drawing functions: blit, text rendering, rectangles, lines, pixels, circles, bargraphs.
 */

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "dll.h"
#include "astonia.h"
#include "sdl/sdl.h"
#include "sdl/sdl_private.h"
#include "sdl/font_manager.h"
#include "sdl/sdl_gpu.h"
#include "sdl/sdl_gpu_draw.h"
#include "sdl/sdl_gpu_atlas.h"
#include "sdl/sdl_gpu_shaderfx.h"
#include "sdl/sdl_gpu_glow.h"
#include "sdl/sdl_gpu_text.h"

#define RENDER_TEXT_LEFT    0
#define RENDER_ALIGN_CENTER 1
#define RENDER_TEXT_RIGHT   2
#define RENDER_TEXT_SHADED  4
#define RENDER_TEXT_LARGE   0
#define RENDER_TEXT_SMALL   8
#define RENDER_TEXT_FRAMED  16
#define RENDER_TEXT_BIG     32
#define RENDER_TEXT_NOCACHE 64

#define RENDER__SHADED_FONT 128
#define RENDER__FRAMED_FONT 256

#define R16TO32(color) (int)(((((color) >> 10) & 31) / 31.0f) * 255.0f)
#define G16TO32(color) (int)(((((color) >> 5) & 31) / 31.0f) * 255.0f)
#define B16TO32(color) (int)((((color) & 31) / 31.0f) * 255.0f)

#define MAXFONTHEIGHT 64

// Current blend mode for rendering operations (used by all drawing functions)
static SDL_BlendMode current_blend_mode = SDL_BLENDMODE_BLEND;

/* GPU-mode counterpart of sdl_blit_tex: blit an SDL_GPUTexture with the same
 * clipping/scale math. `pix_w`/`pix_h` are the SPRITE's dimensions in device
 * pixels (i.e. already multiplied by sdl_scale). For atlas entries the
 * sprite lives at (atlas_x, atlas_y) inside a shared page texture of
 * `tex_w` x `tex_h`; standalone textures pass 0,0 and pix_w/pix_h. Draws
 * into whatever target is currently bound, so it also composes onto
 * offscreen render targets. */
static void sdl_blit_gpu_tex(SDL_GPUTexture *gtex, int atlas_x, int atlas_y, int tex_w, int tex_h, int pix_w, int pix_h,
    int sx, int sy, int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	int addx = 0, addy = 0;
	SDL_FRect dr, sr;
	Uint64 start = SDL_GetTicks();

	if (!gtex || !gpu_draw_is_available()) {
		return;
	}

	int dx = pix_w / sdl_scale;
	int dy = pix_h / sdl_scale;

	if (sx < clipsx) {
		addx = clipsx - sx;
		dx -= addx;
		sx = clipsx;
	}
	if (sy < clipsy) {
		addy = clipsy - sy;
		dy -= addy;
		sy = clipsy;
	}
	if (sx + dx >= clipex) {
		dx = clipex - sx;
	}
	if (sy + dy >= clipey) {
		dy = clipey - sy;
	}

	if (dx <= 0 || dy <= 0) {
		return; // Completely clipped
	}

	dr.x = (float)((sx + x_offset) * sdl_scale);
	dr.y = (float)((sy + y_offset) * sdl_scale);
	dr.w = (float)(dx * sdl_scale);
	dr.h = (float)(dy * sdl_scale);

	sr.x = (float)(atlas_x + addx * sdl_scale);
	sr.y = (float)(atlas_y + addy * sdl_scale);
	sr.w = (float)(dx * sdl_scale);
	sr.h = (float)(dy * sdl_scale);

	/* batch the 1:1 blit as a plain instance when the shader-effects
	 * batch is up and the blend mode is standard - GUI icons, cached
	 * combo textures etc. then merge into the instanced draw runs
	 * instead of paying a pipeline bind + draw call each */
	if (!(gpu_draw_get_blend_mode() == 0 && gpu_shaderfx_plain_quad(gtex, dr.x, dr.y, dr.w, dr.h, (int)sr.x, (int)sr.y,
	                                            (int)sr.w, (int)sr.h, 255, 255, 255, 255))) {
		gpu_draw_texture(gtex, &dr, &sr, tex_w, tex_h, NULL, 255);
	}

	sdl_time_blit += (long long)(SDL_GetTicks() - start);
}

static void sdl_blit_tex(
    SDL_Texture *tex, int sx, int sy, int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	int addx = 0, addy = 0;
	float f_dx, f_dy;
	SDL_FRect dr, sr;
	Uint64 start = SDL_GetTicks();

	// GPU mode never creates SDL_Textures (an SDL_Texture cannot be sampled
	// by SDL_GPU); the GPU-mode equivalent is sdl_blit_gpu_tex above.
	if (use_gpu_rendering) {
		return;
	}

	SDL_GetTextureSize(tex, &f_dx, &f_dy);
	int dx = (int)f_dx;
	int dy = (int)f_dy;

	dx /= sdl_scale;
	dy /= sdl_scale;
	if (sx < clipsx) {
		addx = clipsx - sx;
		dx -= addx;
		sx = clipsx;
	}
	if (sy < clipsy) {
		addy = clipsy - sy;
		dy -= addy;
		sy = clipsy;
	}
	if (sx + dx >= clipex) {
		dx = clipex - sx;
	}
	if (sy + dy >= clipey) {
		dy = clipey - sy;
	}
	dx *= sdl_scale;
	dy *= sdl_scale;

	dr.x = (float)((sx + x_offset) * sdl_scale);
	dr.w = (float)dx;
	dr.y = (float)((sy + y_offset) * sdl_scale);
	dr.h = (float)dy;

	sr.x = (float)(addx * sdl_scale);
	sr.w = (float)dx;
	sr.y = (float)(addy * sdl_scale);
	sr.h = (float)dy;

	SDL_RenderTexture(sdlren, tex, &sr, &dr);

	sdl_time_blit += (long long)(SDL_GetTicks() - start);
}

void sdl_blit(
    int cache_index, int sx, int sy, int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	// GPU path: use GPU texture for direct drawing
	if (use_gpu_rendering && sdlt[cache_index].gpu_tex) {
		// NOTE: this is the parity path (per-draw pipeline binds); the
		// batched shader-effects path is sdl_blit_fx (sdl_gpu_shaderfx.c).
		if (gpu_draw_is_available()) {
			int pix_w = sdlt[cache_index].xres * sdl_scale;
			int pix_h = sdlt[cache_index].yres * sdl_scale;
			int ax = 0, ay = 0, tw = pix_w, th = pix_h;
			if (sdlt[cache_index].atlas_used) {
				// entry lives inside a shared atlas page
				ax = sdlt[cache_index].atlas_x;
				ay = sdlt[cache_index].atlas_y;
				tw = GPU_ATLAS_PAGE_SIZE;
				th = GPU_ATLAS_PAGE_SIZE;
			}
			sdl_blit_gpu_tex(sdlt[cache_index].gpu_tex, ax, ay, tw, th, pix_w, pix_h, sx, sy, clipsx, clipsy, clipex,
			    clipey, x_offset, y_offset);
			return;
		}
	}

	// CPU fallback path
	if (sdlt[cache_index].tex) {
		sdl_blit_tex(sdlt[cache_index].tex, sx, sy, clipsx, clipsy, clipex, clipey, x_offset, y_offset);
	} else if (use_gpu_rendering) {
		// In GPU mode, we have gpu_tex but no SDL texture - this means GPU draw path failed
		static int cpu_fallback_fail_count = 0;
		if (cpu_fallback_fail_count++ < 30) {
			note("sdl_blit: CPU fallback with no SDL tex for sprite=%d (GPU mode, no fallback available)",
			    sdlt[cache_index].sprite);
		}
	}
}

// Helper function to render text to a pixel buffer
// Returns pixel buffer (caller must free) and sets *out_width, *out_height
#ifdef UNIT_TEST
// Non-static so tests/test_text_compare.c can diff the batched glyph
// path against the real string rasterizer (prototype in sdl_private.h)
uint32_t *sdl_rendertext_to_pixels(
    const char *text, struct renderfont *font, uint32_t color, int flags, int *out_width, int *out_height)
#else
static uint32_t *sdl_rendertext_to_pixels(
    const char *text, struct renderfont *font, uint32_t color, int flags, int *out_width, int *out_height)
#endif
{
	uint32_t *pixel, *dst;
	unsigned char *rawrun;
	int x, y = 0, sizex, sizey = 0, sx = 0;
	const char *c;

	for (sizex = 0, c = text; *c && *c != RENDER_TEXT_TERMINATOR; c++) {
		if (*c < 0) {
			continue; /* the raster loop below skips these too */
		}
		sizex += font[(unsigned char)*c].dim * sdl_scale;
	}

	if (flags & (RENDER__FRAMED_FONT | RENDER__SHADED_FONT)) {
		sizex += sdl_scale * 2;
	}

	if (sizex < 1) {
		sizex = 1;
	}

#ifdef SDL_FAST_MALLOC
	pixel = CALLOC((size_t)sizex * MAXFONTHEIGHT, sizeof(uint32_t));
#else
	pixel = xmalloc((int)((size_t)sizex * MAXFONTHEIGHT * sizeof(uint32_t)), MEM_SDL_PIXEL2);
#endif
	if (pixel == NULL) {
		return NULL;
	}

	while (*text && *text != RENDER_TEXT_TERMINATOR) {
		if (*text < 0) {
			note("PANIC: char over limit");
			text++;
			continue;
		}

		rawrun = font[(unsigned char)*text].raw;

		x = sx;
		y = 0;

		dst = pixel + x + y * sizex;

		while (*rawrun != 255) {
			if (*rawrun == 254) {
				y++;
				x = sx;
				rawrun++;
				dst = pixel + x + y * sizex;
				if (y > sizey) {
					sizey = y;
				}
				continue;
			}

			dst += *rawrun;
			x += *rawrun;

			rawrun++;
			*dst = color;
		}
		sx += font[(unsigned char)*text++].dim * sdl_scale;
	}

	if (sizey < 1) {
		sizey = 1;
	}
	sizey++;

	*out_width = sizex;
	*out_height = sizey;
	return pixel;
}

SDL_Texture *sdl_maketext(const char *text, struct renderfont *font, uint32_t color, int flags)
{
	Uint64 start = SDL_GetTicks();

	// GPU path: text handled separately via sdl_maketext_gpu
	if (use_gpu_rendering) {
		return NULL;
	}

	/* TTF path (experimental, opt-in): render via SDL_ttf at device
	 * resolution. On failure fall through to the classic bitmap raster. */
	if (fm_active()) {
		SDL_Surface *surface = fm_render_text_surface(text, color, flags);

		if (surface) {
			SDL_Texture *ttf_tex = SDL_CreateTextureFromSurface(sdlren, surface);

			SDL_DestroySurface(surface);
			sdl_time_text += (long long)(SDL_GetTicks() - start);
			if (ttf_tex) {
				SDL_SetTextureBlendMode(ttf_tex, SDL_BLENDMODE_BLEND);
				return ttf_tex;
			}
			warn("SDL_texture Error: %s maketext ttf (%s)", SDL_GetError(), text);
		}
	}

	int sizex, sizey;
	uint32_t *pixel = sdl_rendertext_to_pixels(text, font, color, flags, &sizex, &sizey);
	if (!pixel) {
		return NULL;
	}

	sdl_time_text += (long long)(SDL_GetTicks() - start);

	start = SDL_GetTicks();
	SDL_Texture *texture = SDL_CreateTexture(sdlren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, sizex, sizey);
	if (texture) {
		SDL_UpdateTexture(texture, NULL, pixel, (int)((size_t)sizex * sizeof(uint32_t)));
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	} else {
		warn("SDL_texture Error: %s maketext (%s)", SDL_GetError(), text);
	}
#ifdef SDL_FAST_MALLOC
	FREE(pixel);
#else
	xfree(pixel);
#endif
	sdl_time_tex += SDL_GetTicks() - start;

	return texture;
}

/* Upload composed text pixels for the GPU path: into a shared atlas
 * page when the batcher is up (the drawtext fast path then draws the
 * string as ONE batched plain quad instead of a pipeline bind + draw),
 * else a standalone texture. Oversized strings fall through to
 * standalone automatically (gpu_atlas_insert caps region size). */
static SDL_GPUTexture *maketext_gpu_upload(
    const uint32_t *pixel, int w, int h, int *out_atlas_x, int *out_atlas_y, uint8_t *out_atlas_used)
{
	*out_atlas_x = 0;
	*out_atlas_y = 0;
	*out_atlas_used = 0;

	if (gpu_shaderfx_ready()) {
		int ax = 0, ay = 0;
		SDL_GPUTexture *page = gpu_atlas_insert(pixel, w, h, &ax, &ay);

		if (page) {
			*out_atlas_x = ax;
			*out_atlas_y = ay;
			*out_atlas_used = 1;
			return page;
		}
	}
	return gpu_texture_create(pixel, w, h);
}

// GPU version of text rendering - creates GPU texture (possibly a
// region inside a shared atlas page, see out_atlas_*)
SDL_GPUTexture *sdl_maketext_gpu(const char *text, struct renderfont *font, uint32_t color, int flags, int *out_width,
    int *out_height, int *out_atlas_x, int *out_atlas_y, uint8_t *out_atlas_used)
{
	Uint64 start = SDL_GetTicks();
	int sizex, sizey;

	*out_atlas_x = 0;
	*out_atlas_y = 0;
	*out_atlas_used = 0;

	/* TTF path (experimental, opt-in): the same SDL_ttf glyphs as the
	 * SDL_Renderer TTF path, uploaded into a GPU texture. On failure fall
	 * through to the classic bitmap raster below. */
	if (fm_active()) {
		SDL_Surface *surface = fm_render_text_surface(text, color, flags);

		if (surface) {
			SDL_Surface *argb = surface;
			SDL_GPUTexture *ttf_tex = NULL;
			int tw = 0, th = 0;

			if (surface->format != SDL_PIXELFORMAT_ARGB8888) {
				argb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
			}
			if (argb) {
				tw = argb->w;
				th = argb->h;
				if (argb->pitch == tw * (int)sizeof(uint32_t)) {
					/* rows are tightly packed - upload directly */
					ttf_tex = maketext_gpu_upload(
					    (const uint32_t *)argb->pixels, tw, th, out_atlas_x, out_atlas_y, out_atlas_used);
				} else {
					/* the uploads expect tightly packed rows */
					uint32_t *tight = malloc((size_t)tw * (size_t)th * sizeof(uint32_t));

					if (tight) {
						for (int row = 0; row < th; row++) {
							memcpy(tight + (size_t)row * (size_t)tw,
							    (const uint8_t *)argb->pixels + (size_t)row * (size_t)argb->pitch,
							    (size_t)tw * sizeof(uint32_t));
						}
						ttf_tex = maketext_gpu_upload(tight, tw, th, out_atlas_x, out_atlas_y, out_atlas_used);
						free(tight);
					}
				}
				if (argb != surface) {
					SDL_DestroySurface(argb);
				}
			}
			SDL_DestroySurface(surface);
			sdl_time_text += (long long)(SDL_GetTicks() - start);
			if (ttf_tex) {
				*out_width = tw;
				*out_height = th;
				return ttf_tex;
			}
			warn("maketext ttf gpu failed (%s)", text);
			start = SDL_GetTicks();
		}
	}

	uint32_t *pixel = sdl_rendertext_to_pixels(text, font, color, flags, &sizex, &sizey);
	if (!pixel) {
		return NULL;
	}

	sdl_time_text += (long long)(SDL_GetTicks() - start);

	start = SDL_GetTicks();
	SDL_GPUTexture *gpu_tex = maketext_gpu_upload(pixel, sizex, sizey, out_atlas_x, out_atlas_y, out_atlas_used);

#ifdef SDL_FAST_MALLOC
	FREE(pixel);
#else
	xfree(pixel);
#endif
	sdl_time_tex += SDL_GetTicks() - start;

	if (gpu_tex) {
		*out_width = sizex;
		*out_height = sizey;
	}
	return gpu_tex;
}

int sdl_drawtext_alpha(int sx, int sy, unsigned short int color, int flags, const char *text, struct renderfont *font,
    int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset, unsigned char alpha)
{
	int dx, cache_index = -1;
	SDL_Texture *tex = NULL;
	int r, g, b, a;
	int aligned_sx;
	const char *c;

	if (!*text) {
		return sx;
	}

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);
	a = 255;

	/* layout advance first - every path needs it for alignment */
	if (fm_active()) {
		/* keep layout advance and alignment in sync with the TTF glyphs */
		dx = fm_text_width(flags, text, -1);
	} else {
		for (dx = 0, c = text; *c && *c != RENDER_TEXT_TERMINATOR; c++) {
			if (*c < 0) {
				continue;
			}
			dx += font[(unsigned char)*c].dim;
		}
	}
	aligned_sx = sx;
	if (flags & RENDER_ALIGN_CENTER) {
		aligned_sx -= dx / 2;
	} else if (flags & RENDER_TEXT_RIGHT) {
		aligned_sx -= dx;
	}

	/* Batched glyph path (bitmap fonts, opaque draws): one atlas quad per
	 * glyph through the sprite_fx batch - no cache entry, no text-hash
	 * walk, no per-string texture. Pixel-identical to the cached-string
	 * draw (tests/test_text_compare.c). Draws with alpha < 255 must not
	 * double-blend overlapping ink (derived _shaded/_framed fonts), so
	 * they use the cached-string quad below instead. */
	if (use_gpu_rendering && !fm_active() && alpha == 255 &&
	    gpu_text_draw_run(text, font, r, g, b, aligned_sx, sy, clipsx, clipsy, clipex, clipey, x_offset, y_offset)) {
		return aligned_sx + dx;
	}

	/* GPU mode: transient GPU textures cannot be destroyed safely while the
	 * frame still references them, so uncached text goes through the texture
	 * cache too (only the debug overlays use RENDER_TEXT_NOCACHE). */
	if ((flags & RENDER_TEXT_NOCACHE) && !use_gpu_rendering) {
		tex = sdl_maketext(text, font, (uint32_t)IRGBA(r, g, b, a), flags);
	} else {
		cache_index = sdl_tx_load(
		    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, text, (int)IRGBA(r, g, b, a), flags, font, 0, 0);
		tex = (cache_index != STX_NONE) ? sdlt[cache_index].tex : NULL;
	}

	// GPU path: use GPU texture if available (cached text only)
	if (use_gpu_rendering && gpu_draw_is_available() && cache_index >= 0 && sdlt[cache_index].gpu_tex) {
		sx = aligned_sx;

		// Text texture dimensions are already in pixels (pre-scaled)
		int tex_w = sdlt[cache_index].xres;
		int tex_h = sdlt[cache_index].yres;

		// Convert texture pixel dimensions to logical dimensions
		int logical_w = tex_w / sdl_scale;
		int logical_h = tex_h / sdl_scale;

		// Apply clipping in logical coordinates
		int draw_x = sx + x_offset;
		int draw_y = sy + y_offset;
		int draw_w = logical_w;
		int draw_h = logical_h;
		int src_x = 0, src_y = 0;

		// Clip left
		if (draw_x < clipsx + x_offset) {
			int clip = clipsx + x_offset - draw_x;
			src_x = clip;
			draw_w -= clip;
			draw_x = clipsx + x_offset;
		}
		// Clip top
		if (draw_y < clipsy + y_offset) {
			int clip = clipsy + y_offset - draw_y;
			src_y = clip;
			draw_h -= clip;
			draw_y = clipsy + y_offset;
		}
		// Clip right
		if (draw_x + draw_w > clipex + x_offset) {
			draw_w = clipex + x_offset - draw_x;
		}
		// Clip bottom
		if (draw_y + draw_h > clipey + y_offset) {
			draw_h = clipey + y_offset - draw_y;
		}

		if (draw_w > 0 && draw_h > 0) {
			/* atlas-resident strings live inside a shared page texture */
			int ax = sdlt[cache_index].atlas_used ? sdlt[cache_index].atlas_x : 0;
			int ay = sdlt[cache_index].atlas_used ? sdlt[cache_index].atlas_y : 0;
			int page_w = sdlt[cache_index].atlas_used ? GPU_ATLAS_PAGE_SIZE : tex_w;
			int page_h = sdlt[cache_index].atlas_used ? GPU_ATLAS_PAGE_SIZE : tex_h;

			// Scale back to pixels for GPU rendering
			SDL_FRect dest = {.x = (float)(draw_x * sdl_scale),
			    .y = (float)(draw_y * sdl_scale),
			    .w = (float)(draw_w * sdl_scale),
			    .h = (float)(draw_h * sdl_scale)};
			SDL_FRect src = {.x = (float)(ax + src_x * sdl_scale),
			    .y = (float)(ay + src_y * sdl_scale),
			    .w = (float)(draw_w * sdl_scale),
			    .h = (float)(draw_h * sdl_scale)};

			/* cached-string fast path: ONE batched plain quad (exact for
			 * every case incl. TTF and alpha fades - the pre-composed
			 * pixels blend once); falls back to the direct draw when the
			 * batch is off or a non-standard blend mode is active */
			if (!(gpu_draw_get_blend_mode() == 0 &&
			        gpu_shaderfx_plain_quad(sdlt[cache_index].gpu_tex, dest.x, dest.y, dest.w, dest.h, (int)src.x,
			            (int)src.y, (int)src.w, (int)src.h, 255, 255, 255, alpha))) {
				gpu_draw_texture(sdlt[cache_index].gpu_tex, &dest, &src, page_w, page_h, NULL, alpha);
			}
		}
		return sx + dx;
	}

	// CPU path (SDL_Renderer)
	if (tex) {
		sx = aligned_sx;

		/* alpha is a draw-time texture mod, not part of the cache key; the
		 * cached texture is shared, so always restore full opacity after */
		if (alpha != 255) {
			SDL_SetTextureAlphaMod(tex, alpha);
		}
		sdl_blit_tex(tex, sx, sy, clipsx, clipsy, clipex, clipey, x_offset, y_offset);
		if (alpha != 255) {
			SDL_SetTextureAlphaMod(tex, 255);
		}

		if (flags & RENDER_TEXT_NOCACHE) {
			SDL_DestroyTexture(tex);
		}
	}

	return sx + dx;
}

int sdl_drawtext(int sx, int sy, unsigned short int color, int flags, const char *text, struct renderfont *font,
    int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	return sdl_drawtext_alpha(
	    sx, sy, color, flags, text, font, clipsx, clipsy, clipex, clipey, x_offset, y_offset, 255);
}

void sdl_rect(int sx, int sy, int ex, int ey, unsigned short int color, int clipsx, int clipsy, int clipex, int clipey,
    int x_offset, int y_offset)
{
	int r, g, b, a;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);
	a = 255;

	// Apply clipping
	if (sx < clipsx) {
		sx = clipsx;
	}
	if (sy < clipsy) {
		sy = clipsy;
	}
	if (ex > clipex) {
		ex = clipex;
	}
	if (ey > clipey) {
		ey = clipey;
	}

	if (sx > ex || sy > ey) {
		return;
	}

	// GPU path: use GPU primitive drawing
	if (use_gpu_rendering && gpu_draw_prim_is_available()) {
		gpu_draw_rect((float)((sx + x_offset) * sdl_scale), (float)((sy + y_offset) * sdl_scale),
		    (float)((ex - sx) * sdl_scale), (float)((ey - sy) * sdl_scale), (float)r / 255.0f, (float)g / 255.0f,
		    (float)b / 255.0f, (float)a / 255.0f);
		return;
	}

	// CPU fallback path
	SDL_FRect rc;
	rc.x = (float)((sx + x_offset) * sdl_scale);
	rc.w = (float)((ex - sx) * sdl_scale);
	rc.y = (float)((sy + y_offset) * sdl_scale);
	rc.h = (float)((ey - sy) * sdl_scale);

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
	SDL_RenderFillRect(sdlren, &rc);
}

void sdl_shaded_rect(int sx, int sy, int ex, int ey, unsigned short int color, unsigned short alpha, int clipsx,
    int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	int r, g, b, a;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);
	a = alpha;

	// Apply clipping
	if (sx < clipsx) {
		sx = clipsx;
	}
	if (sy < clipsy) {
		sy = clipsy;
	}
	if (ex > clipex) {
		ex = clipex;
	}
	if (ey > clipey) {
		ey = clipey;
	}

	if (sx > ex || sy > ey) {
		return;
	}

	// GPU path: use GPU primitive drawing
	if (use_gpu_rendering && gpu_draw_prim_is_available()) {
		gpu_draw_rect((float)((sx + x_offset) * sdl_scale), (float)((sy + y_offset) * sdl_scale),
		    (float)((ex - sx) * sdl_scale), (float)((ey - sy) * sdl_scale), (float)r / 255.0f, (float)g / 255.0f,
		    (float)b / 255.0f, (float)a / 255.0f);
		return;
	}

	// CPU fallback path
	SDL_FRect rc;
	rc.x = (float)((sx + x_offset) * sdl_scale);
	rc.w = (float)((ex - sx) * sdl_scale);
	rc.y = (float)((sy + y_offset) * sdl_scale);
	rc.h = (float)((ey - sy) * sdl_scale);

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);
	SDL_RenderFillRect(sdlren, &rc);
}

void sdl_pixel(int x, int y, unsigned short color, int x_offset, int y_offset)
{
	int r, g, b, a, i;
	SDL_FPoint pt[16];

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);
	a = 255;

	// GPU path: use GPU rectangle for pixel (1x1 logical pixel = sdl_scale x sdl_scale physical)
	if (use_gpu_rendering && gpu_draw_prim_is_available()) {
		gpu_draw_rect((float)((x + x_offset) * sdl_scale), (float)((y + y_offset) * sdl_scale), (float)sdl_scale,
		    (float)sdl_scale, (float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, (float)a / 255.0f);
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
	switch (sdl_scale) {
	case 1:
		SDL_RenderPoint(sdlren, (float)(x + x_offset), (float)(y + y_offset));
		return;
	case 2:
		pt[0].x = (float)((x + x_offset) * sdl_scale);
		pt[0].y = (float)((y + y_offset) * sdl_scale);
		pt[1].x = pt[0].x + 1.0f;
		pt[1].y = pt[0].y;
		pt[2].x = pt[0].x;
		pt[2].y = pt[0].y + 1.0f;
		pt[3].x = pt[0].x + 1.0f;
		pt[3].y = pt[0].y + 1.0f;
		i = 4;
		break;
	case 3:
		pt[0].x = (float)((x + x_offset) * sdl_scale);
		pt[0].y = (float)((y + y_offset) * sdl_scale);
		pt[1].x = pt[0].x + 1.0f;
		pt[1].y = pt[0].y;
		pt[2].x = pt[0].x;
		pt[2].y = pt[0].y + 1.0f;
		pt[3].x = pt[0].x + 1.0f;
		pt[3].y = pt[0].y + 1.0f;
		pt[4].x = pt[0].x + 2.0f;
		pt[4].y = pt[0].y;
		pt[5].x = pt[0].x;
		pt[5].y = pt[0].y + 2.0f;
		pt[6].x = pt[0].x + 2.0f;
		pt[6].y = pt[0].y + 2.0f;
		pt[7].x = pt[0].x + 2.0f;
		pt[7].y = pt[0].y + 1.0f;
		pt[8].x = pt[0].x + 1.0f;
		pt[8].y = pt[0].y + 2.0f;
		i = 9;
		break;
	case 4:
		pt[0].x = (float)((x + x_offset) * sdl_scale);
		pt[0].y = (float)((y + y_offset) * sdl_scale);
		pt[1].x = pt[0].x + 1.0f;
		pt[1].y = pt[0].y;
		pt[2].x = pt[0].x;
		pt[2].y = pt[0].y + 1.0f;
		pt[3].x = pt[0].x + 1.0f;
		pt[3].y = pt[0].y + 1.0f;
		pt[4].x = pt[0].x + 2.0f;
		pt[4].y = pt[0].y;
		pt[5].x = pt[0].x;
		pt[5].y = pt[0].y + 2.0f;
		pt[6].x = pt[0].x + 2.0f;
		pt[6].y = pt[0].y + 2.0f;
		pt[7].x = pt[0].x + 2.0f;
		pt[7].y = pt[0].y + 1.0f;
		pt[8].x = pt[0].x + 1.0f;
		pt[8].y = pt[0].y + 2.0f;
		pt[9].x = pt[0].x + 3.0f;
		pt[9].y = pt[0].y;
		pt[10].x = pt[0].x + 3.0f;
		pt[10].y = pt[0].y + 1.0f;
		pt[11].x = pt[0].x + 3.0f;
		pt[11].y = pt[0].y + 2.0f;
		pt[12].x = pt[0].x + 3.0f;
		pt[12].y = pt[0].y + 3.0f;
		pt[13].x = pt[0].x;
		pt[13].y = pt[0].y + 3.0f;
		pt[14].x = pt[0].x + 1.0f;
		pt[14].y = pt[0].y + 3.0f;
		pt[15].x = pt[0].x + 2.0f;
		pt[15].y = pt[0].y + 3.0f;
		i = 16;
		break;
	default:
		warn("unsupported scale %d in sdl_pixel()", sdl_scale);
		return;
	}
	SDL_RenderPoints(sdlren, pt, i);
}

/* Emit one halo ring of a pretty/rain pixel: 1x1 GPU rects in GPU mode,
 * one batched SDL_RenderPoints call otherwise. Both backends plot the same
 * device-pixel set, so sdl_pretty_pixel()/sdl_rain_pixel() keep a single copy
 * of the geometry (same split as aa_plot() in sdl_line_aa). */
static void halo_points(const SDL_FPoint *pt, int n, int r, int g, int b, int a)
{
	if (use_gpu_rendering) {
		for (int i = 0; i < n; i++) {
			gpu_draw_rect(pt[i].x, pt[i].y, 1.0f, 1.0f, (float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f,
			    (float)a / 255.0f);
		}
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
	SDL_RenderPoints(sdlren, pt, n);
}

/* ====================================================================
 * Spell-effect glows
 *
 * sdl_pretty_pixel() and sdl_rain_pixel() have always been hand-rolled
 * falloffs - a core pixel plus concentric alpha rings, stepped and capped
 * at three device pixels because that is all the SDL_Renderer path could
 * afford. On the GPU the same shapes are one additive capsule each, with
 * a real radial falloff (res/shaders/glow.*), so they come out round and
 * smooth at any radius.
 *
 * The halo is deliberately NOT clipped against the caller's rect: the
 * ring stack it replaces was not clipped either (only the centre is,
 * by the effect code in render.c), so this preserves that behaviour
 * rather than introducing a new edge case.
 * ==================================================================== */

/* Look tuning. Sparkle intensity is deliberately well under 1: the effect
 * code emits five colour steps one animation tick apart at each position,
 * so five glows overlap and sum additively where the CPU rings simply
 * painted over one another. */
#define GLOW_SPARKLE_CORE      2.5f
#define GLOW_SPARKLE_INTENSITY 0.55f
#define GLOW_RAIN_RADIUS       1.2f /* logical px, scaled by sdl_scale */
#define GLOW_RAIN_CORE         1.0f
#define GLOW_RAIN_INTENSITY    0.55f
#define GLOW_SATURATION        2.5f /* exponent on the non-dominant channels */

int sdl_fancy_effects_active(void)
{
	return use_gpu_rendering && !(game_options & GO_NOFANCYFX) && gpu_glow_ready();
}

/* Halo radius for a single effect sparkle, in device pixels. The ring
 * stack reached 2 device px at sdl_scale 2 and 3 at 3/4; a real falloff
 * needs a little more room to read as light rather than a dot. */
static float glow_pixel_radius(void)
{
	return 0.7f * (float)sdl_scale + 0.6f;
}

/* Emit one capsule glow in device pixels. Colour is the game's 15-bit
 * format; intensity scales the additive contribution. */
/* Push a colour towards its dominant channel.
 *
 * The effect palette is deliberately pale - bless is IRGB(24,24,31),
 * barely off white - because the CPU path PAINTED those pixels, so the
 * hue read fine against any background. Added light does not behave that
 * way: over bright ground the red and green channels clip first and a
 * pale blue sparkle turns into a grey smear. Raising the non-dominant
 * channels to a power keeps the hue the palette intends, and leaves
 * genuinely neutral colours alone. */
static void glow_saturate(float *r, float *g, float *b)
{
	float mx = *r;

	if (*g > mx) {
		mx = *g;
	}
	if (*b > mx) {
		mx = *b;
	}
	if (mx <= 0.0f) {
		return;
	}
	*r = mx * powf(*r / mx, GLOW_SATURATION);
	*g = mx * powf(*g / mx, GLOW_SATURATION);
	*b = mx * powf(*b / mx, GLOW_SATURATION);
}

static bool glow_emit(
    float x0, float y0, float x1, float y1, unsigned short color, float radius, float core, float intensity)
{
	float r = (float)R16TO32(color) / 255.0f;
	float g = (float)G16TO32(color) / 255.0f;
	float b = (float)B16TO32(color) / 255.0f;

	glow_saturate(&r, &g, &b);
	return gpu_glow_add(x0, y0, x1, y1, radius, core, r, g, b, intensity);
}

void sdl_glow_line(int fx, int fy, int tx, int ty, unsigned short color, float radius, float core, float intensity,
    int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	if (!sdl_fancy_effects_active()) {
		return;
	}

	/* cheap reject: drop capsules whose padded bounding box misses the
	 * clip rect entirely */
	int pad = (int)(radius + 1.0f);
	if (max(fx, tx) + pad < clipsx || min(fx, tx) - pad >= clipex || max(fy, ty) + pad < clipsy ||
	    min(fy, ty) - pad >= clipey) {
		return;
	}

	glow_emit((float)((fx + x_offset) * sdl_scale), (float)((fy + y_offset) * sdl_scale),
	    (float)((tx + x_offset) * sdl_scale), (float)((ty + y_offset) * sdl_scale), color, radius * (float)sdl_scale,
	    core, intensity);
}

void sdl_pretty_pixel(int x, int y, unsigned short color, int x_offset, int y_offset)
{
	int r, g, b;
	float px, py;
	SDL_FPoint pt[16];

	// GPU path plots the same halo as 1x1 rects (via halo_points)
	if (use_gpu_rendering && !gpu_draw_prim_is_available()) {
		return;
	}

	px = (float)((x + x_offset) * sdl_scale);
	py = (float)((y + y_offset) * sdl_scale);

	// one soft additive sparkle instead of the ring stack
	if (sdl_fancy_effects_active() &&
	    glow_emit(px, py, px, py, color, glow_pixel_radius(), GLOW_SPARKLE_CORE, GLOW_SPARKLE_INTENSITY)) {
		return;
	}

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	switch (sdl_scale) {
	case 1:
		pt[0].x = px;
		pt[0].y = py;
		halo_points(pt, 1, r, g, b, 255);
		break;
	case 2:
		pt[0].x = px;
		pt[0].y = py;
		halo_points(pt, 1, min(r + 64, 255), min(g + 64, 255), min(b + 64, 255), 255);

		pt[0].x = px + 1.0f;
		pt[0].y = py;

		pt[1].x = px;
		pt[1].y = py + 1.0f;

		pt[2].x = px - 1.0f;
		pt[2].y = py;

		pt[3].x = px;
		pt[3].y = py - 1.0f;

		halo_points(pt, 4, r, g, b, 128);

		pt[0].x = px + 2.0f;
		pt[0].y = py;

		pt[1].x = px;
		pt[1].y = py + 2.0f;

		pt[2].x = px - 2.0f;
		pt[2].y = py;

		pt[3].x = px;
		pt[3].y = py - 2.0f;

		pt[4].x = px + 1.0f;
		pt[4].y = py + 1.0f;

		pt[5].x = px + 1.0f;
		pt[5].y = py - 1.0f;

		pt[6].x = px - 1.0f;
		pt[6].y = py + 1.0f;

		pt[7].x = px - 1.0f;
		pt[7].y = py - 1.0f;

		halo_points(pt, 8, r, g, b, 64);
		break;
	case 3:
	case 4:
		pt[0].x = px;
		pt[0].y = py;
		halo_points(pt, 1, min(r + 64, 255), min(g + 64, 255), min(b + 64, 255), 255);

		pt[0].x = px + 1.0f;
		pt[0].y = py;

		pt[1].x = px;
		pt[1].y = py + 1.0f;

		pt[2].x = px - 1.0f;
		pt[2].y = py;

		pt[3].x = px;
		pt[3].y = py - 1.0f;

		halo_points(pt, 4, min(r + 32, 255), min(g + 32, 255), min(b + 32, 255), sdl_scale == 4 ? 192 : 128);

		pt[0].x = px + 2.0f;
		pt[0].y = py;

		pt[1].x = px;
		pt[1].y = py + 2.0f;

		pt[2].x = px - 2.0f;
		pt[2].y = py;

		pt[3].x = px;
		pt[3].y = py - 2.0f;

		pt[4].x = px + 1.0f;
		pt[4].y = py + 1.0f;

		pt[5].x = px + 1.0f;
		pt[5].y = py - 1.0f;

		pt[6].x = px - 1.0f;
		pt[6].y = py + 1.0f;

		pt[7].x = px - 1.0f;
		pt[7].y = py - 1.0f;

		halo_points(pt, 8, r, g, b, sdl_scale == 4 ? 128 : 64);

		pt[0].x = px + 3.0f;
		pt[0].y = py;

		pt[1].x = px;
		pt[1].y = py + 3.0f;

		pt[2].x = px - 3.0f;
		pt[2].y = py;

		pt[3].x = px;
		pt[3].y = py - 3.0f;

		pt[4].x = px + 2.0f;
		pt[4].y = py + 1.0f;

		pt[5].x = px + 2.0f;
		pt[5].y = py - 1.0f;

		pt[6].x = px - 2.0f;
		pt[6].y = py + 1.0f;

		pt[7].x = px - 2.0f;
		pt[7].y = py - 1.0f;

		pt[8].x = px + 1.0f;
		pt[8].y = py + 2.0f;

		pt[9].x = px + 1.0f;
		pt[9].y = py - 2.0f;

		pt[10].x = px - 1.0f;
		pt[10].y = py + 2.0f;

		pt[11].x = px - 1.0f;
		pt[11].y = py - 2.0f;

		halo_points(pt, 12, r, g, b, sdl_scale == 4 ? 64 : 32);
		break;
	default:
		warn("unsupported scale %d in sdl_pretty_pixel()", sdl_scale);
		return;
	}
}

void sdl_rain_pixel(int x, int y, unsigned short color, int x_offset, int y_offset)
{
	int r, g, b;
	float px, py;
	SDL_FPoint pt[16];

	// GPU path plots the same droplet as 1x1 rects (via halo_points)
	if (use_gpu_rendering && !gpu_draw_prim_is_available()) {
		return;
	}

	px = (float)((x + x_offset) * sdl_scale);
	py = (float)((y + y_offset) * sdl_scale);

	// the droplet is a streak, so one capsule along its length
	if (sdl_fancy_effects_active() && glow_emit(px, py, px, py - 3.0f * (float)sdl_scale, color,
	                                      GLOW_RAIN_RADIUS * (float)sdl_scale, GLOW_RAIN_CORE, GLOW_RAIN_INTENSITY)) {
		return;
	}

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	switch (sdl_scale) {
	case 1:
		pt[0].x = px;
		pt[0].y = py;
		halo_points(pt, 1, r, g, b, 255);
		break;
	case 2:
		pt[0].x = px;
		pt[0].y = py;

		pt[1].x = px;
		pt[1].y = py - 1.0f;

		halo_points(pt, 2, min(r + 64, 255), min(g + 64, 255), min(b + 64, 255), 255);

		pt[0].x = px + 1.0f;
		pt[0].y = py;

		pt[1].x = px;
		pt[1].y = py + 1.0f;

		pt[2].x = px - 1.0f;
		pt[2].y = py;

		pt[3].x = px;
		pt[3].y = py - 2.0f;

		halo_points(pt, 4, r, g, b, 128);

		pt[0].x = px + 1.0f;
		pt[0].y = py - 1.0f;

		pt[1].x = px - 1.0f;
		pt[1].y = py - 1.0f;

		pt[2].x = px - 1.0f;
		pt[2].y = py - 2.0f;

		pt[3].x = px + 1.0f;
		pt[3].y = py - 2.0f;

		pt[4].x = px;
		pt[4].y = py - 3.0f;

		halo_points(pt, 5, r, g, b, 64);
		break;
	case 3:
	case 4:
		pt[0].x = px;
		pt[0].y = py;

		pt[1].x = px;
		pt[1].y = py - 1.0f;

		halo_points(pt, 2, min(r + 64, 255), min(g + 64, 255), min(b + 64, 255), 255);

		pt[0].x = px + 1.0f;
		pt[0].y = py;

		pt[1].x = px;
		pt[1].y = py + 1.0f;

		pt[2].x = px - 1.0f;
		pt[2].y = py;

		pt[3].x = px;
		pt[3].y = py - 2.0f;

		halo_points(pt, 4, min(r + 32, 255), min(g + 32, 255), min(b + 32, 255), sdl_scale == 4 ? 192 : 128);

		pt[0].x = px + 1.0f;
		pt[0].y = py - 1.0f;

		pt[1].x = px - 1.0f;
		pt[1].y = py - 1.0f;

		pt[2].x = px - 1.0f;
		pt[2].y = py - 2.0f;

		pt[3].x = px + 1.0f;
		pt[3].y = py - 2.0f;

		pt[4].x = px;
		pt[4].y = py - 3.0f;

		halo_points(pt, 5, r, g, b, sdl_scale == 4 ? 128 : 64);

		pt[0].x = px + 2.0f;
		pt[0].y = py - 1.0f;

		pt[1].x = px - 2.0f;
		pt[1].y = py - 1.0f;

		pt[2].x = px + 2.0f;
		pt[2].y = py - 2.0f;

		pt[3].x = px - 2.0f;
		pt[3].y = py - 2.0f;

		pt[4].x = px + 1.0f;
		pt[4].y = py - 3.0f;

		pt[5].x = px - 1.0f;
		pt[5].y = py - 3.0f;

		pt[6].x = px + 2.0f;
		pt[6].y = py - 3.0f;

		pt[7].x = px - 2.0f;
		pt[7].y = py - 3.0f;

		halo_points(pt, 8, r, g, b, sdl_scale == 4 ? 64 : 32);
		break;
	default:
		warn("unsupported scale %d in sdl_rain_pixel()", sdl_scale);
		return;
	}
}

void sdl_line(int fx, int fy, int tx, int ty, unsigned short color, int clipsx, int clipsy, int clipex, int clipey,
    int x_offset, int y_offset)
{
	int r, g, b, a;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);
	a = 255;

	// GPU path: use GPU line drawing
	if (use_gpu_rendering && gpu_draw_line_is_available()) {
		// Clipping
		if (fx < clipsx) {
			fx = clipsx;
		}
		if (fy < clipsy) {
			fy = clipsy;
		}
		if (fx >= clipex) {
			fx = clipex - 1;
		}
		if (fy >= clipey) {
			fy = clipey - 1;
		}
		if (tx < clipsx) {
			tx = clipsx;
		}
		if (ty < clipsy) {
			ty = clipsy;
		}
		if (tx >= clipex) {
			tx = clipex - 1;
		}
		if (ty >= clipey) {
			ty = clipey - 1;
		}

		gpu_draw_line((float)((fx + x_offset) * sdl_scale), (float)((fy + y_offset) * sdl_scale),
		    (float)((tx + x_offset) * sdl_scale), (float)((ty + y_offset) * sdl_scale), (float)r / 255.0f,
		    (float)g / 255.0f, (float)b / 255.0f, (float)a / 255.0f);
		return;
	}

	if (fx < clipsx) {
		fx = clipsx;
	}
	if (fy < clipsy) {
		fy = clipsy;
	}
	if (fx >= clipex) {
		fx = clipex - 1;
	}
	if (fy >= clipey) {
		fy = clipey - 1;
	}

	if (tx < clipsx) {
		tx = clipsx;
	}
	if (ty < clipsy) {
		ty = clipsy;
	}
	if (tx >= clipex) {
		tx = clipex - 1;
	}
	if (ty >= clipey) {
		ty = clipey - 1;
	}

	fx += x_offset;
	tx += x_offset;
	fy += y_offset;
	ty += y_offset;

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
	// TODO: This is a thinner line when scaled up. It looks surprisingly good. Maybe keep it this way?
	SDL_RenderLine(
	    sdlren, (float)(fx * sdl_scale), (float)(fy * sdl_scale), (float)(tx * sdl_scale), (float)(ty * sdl_scale));
}

void sdl_bargraph_add(int dx, unsigned char *data, int val)
{
	memmove(data + 1, data, (size_t)(dx - 1));
	data[0] = (unsigned char)val;
}

void sdl_bargraph(int sx, int sy, int dx, unsigned char *data, int x_offset, int y_offset)
{
	int n;

	// GPU path: use GPU line drawing
	if (use_gpu_rendering && gpu_draw_line_is_available()) {
		for (n = 0; n < dx; n++) {
			float r, g, b;
			if (data[n] > 40) {
				r = 255.0f / 255.0f;
				g = 80.0f / 255.0f;
				b = 80.0f / 255.0f;
			} else {
				r = 80.0f / 255.0f;
				g = 255.0f / 255.0f;
				b = 80.0f / 255.0f;
			}
			gpu_draw_line((float)((sx + n + x_offset) * sdl_scale), (float)((sy + y_offset) * sdl_scale),
			    (float)((sx + n + x_offset) * sdl_scale), (float)((sy - data[n] + y_offset) * sdl_scale), r, g, b,
			    127.0f / 255.0f);
		}
		return;
	}

	for (n = 0; n < dx; n++) {
		if (data[n] > 40) {
			SDL_SetRenderDrawColor(sdlren, 255, 80, 80, 127);
		} else {
			SDL_SetRenderDrawColor(sdlren, 80, 255, 80, 127);
		}

		SDL_RenderLine(sdlren, (float)((sx + n + x_offset) * sdl_scale), (float)((sy + y_offset) * sdl_scale),
		    (float)((sx + n + x_offset) * sdl_scale), (float)((sy - data[n] + y_offset) * sdl_scale));
	}
}

void sdl_pixel_alpha(int x, int y, unsigned short color, unsigned char alpha, int x_offset, int y_offset)
{
	int r, g, b, i;
	SDL_FPoint pt[16];

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	// GPU path: use GPU rectangle for pixel (1x1 logical pixel = sdl_scale x sdl_scale physical)
	if (use_gpu_rendering && gpu_draw_prim_is_available()) {
		gpu_draw_rect((float)((x + x_offset) * sdl_scale), (float)((y + y_offset) * sdl_scale), (float)sdl_scale,
		    (float)sdl_scale, (float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, (float)alpha / 255.0f);
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);
	switch (sdl_scale) {
	case 1:
		SDL_RenderPoint(sdlren, (float)(x + x_offset), (float)(y + y_offset));
		return;
	case 2:
		pt[0].x = (float)((x + x_offset) * sdl_scale);
		pt[0].y = (float)((y + y_offset) * sdl_scale);
		pt[1].x = pt[0].x + 1;
		pt[1].y = pt[0].y;
		pt[2].x = pt[0].x;
		pt[2].y = pt[0].y + 1;
		pt[3].x = pt[0].x + 1;
		pt[3].y = pt[0].y + 1;
		i = 4;
		break;
	case 3:
		pt[0].x = (float)((x + x_offset) * sdl_scale);
		pt[0].y = (float)((y + y_offset) * sdl_scale);
		pt[1].x = pt[0].x + 1;
		pt[1].y = pt[0].y;
		pt[2].x = pt[0].x;
		pt[2].y = pt[0].y + 1;
		pt[3].x = pt[0].x + 1;
		pt[3].y = pt[0].y + 1;
		pt[4].x = pt[0].x + 2;
		pt[4].y = pt[0].y;
		pt[5].x = pt[0].x;
		pt[5].y = pt[0].y + 2;
		pt[6].x = pt[0].x + 2;
		pt[6].y = pt[0].y + 2;
		pt[7].x = pt[0].x + 2;
		pt[7].y = pt[0].y + 1;
		pt[8].x = pt[0].x + 1;
		pt[8].y = pt[0].y + 2;
		i = 9;
		break;
	case 4:
		pt[0].x = (float)((x + x_offset) * sdl_scale);
		pt[0].y = (float)((y + y_offset) * sdl_scale);
		pt[1].x = pt[0].x + 1;
		pt[1].y = pt[0].y;
		pt[2].x = pt[0].x;
		pt[2].y = pt[0].y + 1;
		pt[3].x = pt[0].x + 1;
		pt[3].y = pt[0].y + 1;
		pt[4].x = pt[0].x + 2;
		pt[4].y = pt[0].y;
		pt[5].x = pt[0].x;
		pt[5].y = pt[0].y + 2;
		pt[6].x = pt[0].x + 2;
		pt[6].y = pt[0].y + 2;
		pt[7].x = pt[0].x + 2;
		pt[7].y = pt[0].y + 1;
		pt[8].x = pt[0].x + 1;
		pt[8].y = pt[0].y + 2;
		pt[9].x = pt[0].x + 3;
		pt[9].y = pt[0].y;
		pt[10].x = pt[0].x + 3;
		pt[10].y = pt[0].y + 1;
		pt[11].x = pt[0].x + 3;
		pt[11].y = pt[0].y + 2;
		pt[12].x = pt[0].x + 3;
		pt[12].y = pt[0].y + 3;
		pt[13].x = pt[0].x;
		pt[13].y = pt[0].y + 3;
		pt[14].x = pt[0].x + 1;
		pt[14].y = pt[0].y + 3;
		pt[15].x = pt[0].x + 2;
		pt[15].y = pt[0].y + 3;
		i = 16;
		break;
	default:
		warn("unsupported scale %d in sdl_pixel_alpha()", sdl_scale);
		return;
	}
	SDL_RenderPoints(sdlren, pt, i);
}

// Cohen-Sutherland outcodes for line clipping
#define CLIP_INSIDE 0 // 0000
#define CLIP_LEFT   1 // 0001
#define CLIP_RIGHT  2 // 0010
#define CLIP_BOTTOM 4 // 0100
#define CLIP_TOP    8 // 1000

static int compute_outcode(int x, int y, int xmin, int ymin, int xmax, int ymax)
{
	int code = CLIP_INSIDE;
	if (x < xmin) {
		code |= CLIP_LEFT;
	} else if (x > xmax) {
		code |= CLIP_RIGHT;
	}
	if (y < ymin) {
		code |= CLIP_BOTTOM;
	} else if (y > ymax) {
		code |= CLIP_TOP;
	}
	return code;
}

// Cohen-Sutherland line clipping algorithm - preserves line slope
// Returns 1 if line should be drawn (and modifies coordinates), 0 if completely outside
#ifdef UNIT_TEST
// Non-static for unit testing (prototype in sdl_private.h)
int clip_line(int *x0, int *y0, int *x1, int *y1, int xmin, int ymin, int xmax, int ymax)
#else
static int clip_line(int *x0, int *y0, int *x1, int *y1, int xmin, int ymin, int xmax, int ymax)
#endif
{
	int outcode0 = compute_outcode(*x0, *y0, xmin, ymin, xmax, ymax);
	int outcode1 = compute_outcode(*x1, *y1, xmin, ymin, xmax, ymax);

	while (1) {
		if (!(outcode0 | outcode1)) {
			// Both endpoints inside - draw it
			return 1;
		} else if (outcode0 & outcode1) {
			// Both endpoints outside same region - reject
			return 0;
		} else {
			// Line crosses boundary - clip it
			int x, y;
			int outcodeOut = outcode0 ? outcode0 : outcode1;

			// Find intersection point using line equation
			// Avoid division by zero by checking dx/dy
			if (outcodeOut & CLIP_TOP) {
				if (*y1 != *y0) {
					x = *x0 + (*x1 - *x0) * (ymax - *y0) / (*y1 - *y0);
				} else {
					x = *x0;
				}
				y = ymax;
			} else if (outcodeOut & CLIP_BOTTOM) {
				if (*y1 != *y0) {
					x = *x0 + (*x1 - *x0) * (ymin - *y0) / (*y1 - *y0);
				} else {
					x = *x0;
				}
				y = ymin;
			} else if (outcodeOut & CLIP_RIGHT) {
				if (*x1 != *x0) {
					y = *y0 + (*y1 - *y0) * (xmax - *x0) / (*x1 - *x0);
				} else {
					y = *y0;
				}
				x = xmax;
			} else { // CLIP_LEFT
				if (*x1 != *x0) {
					y = *y0 + (*y1 - *y0) * (xmin - *x0) / (*x1 - *x0);
				} else {
					y = *y0;
				}
				x = xmin;
			}

			// Replace endpoint outside clip rectangle
			if (outcodeOut == outcode0) {
				*x0 = x;
				*y0 = y;
				outcode0 = compute_outcode(*x0, *y0, xmin, ymin, xmax, ymax);
			} else {
				*x1 = x;
				*y1 = y;
				outcode1 = compute_outcode(*x1, *y1, xmin, ymin, xmax, ymax);
			}
		}
	}
}

void sdl_line_alpha(int fx, int fy, int tx, int ty, unsigned short color, unsigned char alpha, int clipsx, int clipsy,
    int clipex, int clipey, int x_offset, int y_offset)
{
	int r, g, b;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	// Properly clip the line (preserves slope) using Cohen-Sutherland algorithm
	if (!clip_line(&fx, &fy, &tx, &ty, clipsx, clipsy, clipex - 1, clipey - 1)) {
		// Line is completely outside clip rect
		return;
	}

	// GPU path: use GPU line drawing
	if (use_gpu_rendering && gpu_draw_line_is_available()) {
		gpu_draw_line((float)((fx + x_offset) * sdl_scale), (float)((fy + y_offset) * sdl_scale),
		    (float)((tx + x_offset) * sdl_scale), (float)((ty + y_offset) * sdl_scale), (float)r / 255.0f,
		    (float)g / 255.0f, (float)b / 255.0f, (float)alpha / 255.0f);
		return;
	}

	// Apply offset
	fx += x_offset;
	tx += x_offset;
	fy += y_offset;
	ty += y_offset;

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);
	SDL_RenderLine(
	    sdlren, (float)(fx * sdl_scale), (float)(fy * sdl_scale), (float)(tx * sdl_scale), (float)(ty * sdl_scale));
}

void sdl_set_blend_mode(int mode)
{
	switch (mode) {
	case 0: // BLEND_NORMAL
		current_blend_mode = SDL_BLENDMODE_BLEND;
		break;
	case 1: // BLEND_ADDITIVE
		current_blend_mode = SDL_BLENDMODE_ADD;
		break;
	case 2: // BLEND_MOD
		current_blend_mode = SDL_BLENDMODE_MOD;
		break;
	case 3: // BLEND_MUL
		current_blend_mode = SDL_BLENDMODE_MUL;
		break;
	case 4: // BLEND_NONE
		current_blend_mode = SDL_BLENDMODE_NONE;
		break;
	default:
		current_blend_mode = SDL_BLENDMODE_BLEND;
		break;
	}
	if (use_gpu_rendering) {
		// GPU path: select the matching blend pipeline variant
		gpu_draw_set_blend_mode((mode >= 0 && mode < GPU_DRAW_BLEND_MODES) ? mode : 0);
	} else {
		SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);
	}
}

int sdl_get_blend_mode(void)
{
	switch (current_blend_mode) {
	case SDL_BLENDMODE_BLEND:
		return 0; // BLEND_NORMAL
	case SDL_BLENDMODE_ADD:
		return 1; // BLEND_ADDITIVE
	case SDL_BLENDMODE_MOD:
		return 2; // BLEND_MOD
	case SDL_BLENDMODE_MUL:
		return 3; // BLEND_MUL
	case SDL_BLENDMODE_NONE:
		return 4; // BLEND_NONE
	default:
		return 0;
	}
}

void sdl_reset_blend_mode(void)
{
	current_blend_mode = SDL_BLENDMODE_BLEND;
	if (use_gpu_rendering) {
		gpu_draw_set_blend_mode(0);
	} else {
		SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);
	}
}

// ============================================================================
// Custom Texture Loading API for Modders
// ============================================================================

#define MAX_MOD_TEXTURES 256

struct mod_texture {
	SDL_Texture *tex; // SDL_Renderer path
	SDL_GPUTexture *gpu_tex; // SDL_GPU path
	int width;
	int height;
	int used;
};

static struct mod_texture mod_textures[MAX_MOD_TEXTURES];
static int mod_textures_initialized = 0;

static void init_mod_textures(void)
{
	if (!mod_textures_initialized) {
		memset(mod_textures, 0, sizeof(mod_textures));
		mod_textures_initialized = 1;
	}
}

// Cleanup all mod textures - called from sdl_exit() for clean shutdown
// Gated behind DEVELOPER for cleaner valgrind/address sanitizer reports
void sdl_cleanup_mod_textures(void)
{
#ifdef DEVELOPER
	if (!mod_textures_initialized) {
		return;
	}
	for (int i = 0; i < MAX_MOD_TEXTURES; i++) {
		if (mod_textures[i].used && mod_textures[i].tex) {
			SDL_DestroyTexture(mod_textures[i].tex);
			mod_textures[i].tex = NULL;
		}
		if (mod_textures[i].used && mod_textures[i].gpu_tex) {
			gpu_texture_destroy(mod_textures[i].gpu_tex);
			mod_textures[i].gpu_tex = NULL;
		}
		mod_textures[i].used = 0;
	}
	mod_textures_initialized = 0;
#endif
}

/**
 * Validate a path for safety before loading.
 * Rejects path traversal attempts and absolute paths.
 * Uses conservative checks to prevent bypasses like "..././" or "....//".
 */
static int validate_mod_path(const char *path)
{
	const char *p;
	size_t len;

	if (!path || !*path) {
		return 0;
	}

	len = strlen(path);

	// Reject paths that are too long (prevent buffer overflows)
	if (len > 1024) {
		warn("mod texture: path too long: %s", path);
		return 0;
	}

	// Reject absolute paths (Unix or Windows style)
	if (path[0] == '/' || path[0] == '\\') {
		warn("mod texture: absolute paths not allowed: %s", path);
		return 0;
	}
	if (len >= 2 && path[1] == ':') {
		warn("mod texture: absolute paths not allowed: %s", path);
		return 0;
	}

	// Check each character for dangerous patterns
	p = path;
	while (*p) {
		// Reject embedded null bytes (shouldn't happen with strlen, but be safe)
		if (*p == '\0') {
			break;
		}

		// Reject any ".." sequence - conservative but safe
		// This catches: "..", "../", "..\\", "...", "..foo", etc.
		// Only legitimate ".." is as a directory component, which we don't want anyway
		if (p[0] == '.' && p[1] == '.') {
			// Check if this could be a traversal attempt
			// Reject if: at start, after separator, or followed by separator/end
			int at_start = (p == path);
			int after_sep = (p > path && (p[-1] == '/' || p[-1] == '\\'));
			int before_sep = (p[2] == '\0' || p[2] == '/' || p[2] == '\\');
			int before_dot = (p[2] == '.'); // catches "...", "..../" etc.

			if ((at_start || after_sep) && (before_sep || before_dot)) {
				warn("mod texture: path traversal not allowed: %s", path);
				return 0;
			}
		}

		p++;
	}

	return 1;
}

int sdl_load_mod_texture(const char *path)
{
	int dx, dy;
	uint32_t *pixel;
	SDL_Texture *tex = NULL;
	SDL_GPUTexture *gpu_tex = NULL;
	int i;

	init_mod_textures();

	// Security: validate path before loading
	if (!validate_mod_path(path)) {
		return -1;
	}

	// Find free slot
	for (i = 0; i < MAX_MOD_TEXTURES; i++) {
		if (!mod_textures[i].used) {
			break;
		}
	}
	if (i >= MAX_MOD_TEXTURES) {
		warn("mod texture slots exhausted");
		return -1;
	}

	// Load PNG (sdl_load_png takes non-const path for historical reasons)
	char path_copy[512];
	snprintf(path_copy, sizeof(path_copy), "%s", path);
	pixel = sdl_load_png(path_copy, &dx, &dy);
	if (!pixel) {
		warn("failed to load mod texture: %s", path);
		return -1;
	}

	if (use_gpu_rendering) {
		// GPU path: upload directly as a GPU texture
		gpu_tex = gpu_texture_create(pixel, dx, dy);
		if (!gpu_tex) {
			warn("failed to create GPU mod texture: %s", path);
#ifdef SDL_FAST_MALLOC
			FREE(pixel);
#else
			xfree(pixel);
#endif
			return -1;
		}
	} else {
		// Create SDL texture
		tex = SDL_CreateTexture(sdlren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, dx, dy);
		if (!tex) {
			warn("failed to create SDL texture: %s", SDL_GetError());
#ifdef SDL_FAST_MALLOC
			FREE(pixel);
#else
			xfree(pixel);
#endif
			return -1;
		}

		SDL_UpdateTexture(tex, NULL, pixel, dx * (int)sizeof(uint32_t));
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	}

#ifdef SDL_FAST_MALLOC
	FREE(pixel);
#else
	xfree(pixel);
#endif

	mod_textures[i].tex = tex;
	mod_textures[i].gpu_tex = gpu_tex;
	mod_textures[i].width = dx;
	mod_textures[i].height = dy;
	mod_textures[i].used = 1;

	return i;
}

void sdl_unload_mod_texture(int tex_id)
{
	if (tex_id < 0 || tex_id >= MAX_MOD_TEXTURES) {
		return;
	}
	if (!mod_textures[tex_id].used) {
		return;
	}

	if (mod_textures[tex_id].tex) {
		SDL_DestroyTexture(mod_textures[tex_id].tex);
	}
	if (mod_textures[tex_id].gpu_tex) {
		gpu_texture_destroy(mod_textures[tex_id].gpu_tex);
	}
	mod_textures[tex_id].tex = NULL;
	mod_textures[tex_id].gpu_tex = NULL;
	mod_textures[tex_id].width = 0;
	mod_textures[tex_id].height = 0;
	mod_textures[tex_id].used = 0;
}

void sdl_render_mod_texture(int tex_id, int x, int y, unsigned char alpha, int clipsx, int clipsy, int clipex,
    int clipey, int x_offset, int y_offset)
{
	SDL_FRect dr, sr;
	int dx, dy, addx = 0, addy = 0;

	if (tex_id < 0 || tex_id >= MAX_MOD_TEXTURES) {
		return;
	}
	if (!mod_textures[tex_id].used) {
		return;
	}
	if (use_gpu_rendering ? !mod_textures[tex_id].gpu_tex : !mod_textures[tex_id].tex) {
		return;
	}

	dx = mod_textures[tex_id].width;
	dy = mod_textures[tex_id].height;

	// Apply clipping
	if (x < clipsx) {
		addx = clipsx - x;
		dx -= addx;
		x = clipsx;
	}
	if (y < clipsy) {
		addy = clipsy - y;
		dy -= addy;
		y = clipsy;
	}
	if (x + dx > clipex) {
		dx = clipex - x;
	}
	if (y + dy > clipey) {
		dy = clipey - y;
	}
	if (dx <= 0 || dy <= 0) {
		return;
	}

	sr.x = (float)addx;
	sr.y = (float)addy;
	sr.w = (float)dx;
	sr.h = (float)dy;

	dr.x = (float)((x + x_offset) * sdl_scale);
	dr.y = (float)((y + y_offset) * sdl_scale);
	dr.w = (float)(dx * sdl_scale);
	dr.h = (float)(dy * sdl_scale);

	if (use_gpu_rendering) {
		if (gpu_draw_is_available()) {
			/* unscaled mod-texture blit (dest = src * sdl_scale, an
			 * integer nearest upscale - tie-free, so the batched texel
			 * math matches the sampler exactly): joins the plain-instance
			 * batch when possible. The float-scaled variant below stays
			 * direct - fractional nearest resampling has boundary ties
			 * where the batched path is not guaranteed to match. */
			if (!(gpu_draw_get_blend_mode() == 0 &&
			        gpu_shaderfx_plain_quad(mod_textures[tex_id].gpu_tex, dr.x, dr.y, dr.w, dr.h, (int)sr.x, (int)sr.y,
			            (int)sr.w, (int)sr.h, 255, 255, 255, alpha))) {
				gpu_draw_texture(mod_textures[tex_id].gpu_tex, &dr, &sr, mod_textures[tex_id].width,
				    mod_textures[tex_id].height, NULL, alpha);
			}
		}
		return;
	}

	SDL_SetTextureAlphaMod(mod_textures[tex_id].tex, alpha);
	SDL_RenderTexture(sdlren, mod_textures[tex_id].tex, &sr, &dr);
}

void sdl_render_mod_texture_scaled(int tex_id, int x, int y, float scale, unsigned char alpha, int clipsx, int clipsy,
    int clipex, int clipey, int x_offset, int y_offset)
{
	SDL_FRect dr, sr;
	int dx, dy, scaled_dx, scaled_dy;

	if (tex_id < 0 || tex_id >= MAX_MOD_TEXTURES) {
		return;
	}
	if (!mod_textures[tex_id].used) {
		return;
	}
	if (use_gpu_rendering ? !mod_textures[tex_id].gpu_tex : !mod_textures[tex_id].tex) {
		return;
	}
	// Security: validate scale to prevent integer overflow
	// Max scale of 100x is generous while preventing overflow with large textures
	if (scale <= 0.0f || scale > 100.0f) {
		return;
	}

	dx = mod_textures[tex_id].width;
	dy = mod_textures[tex_id].height;
	scaled_dx = (int)((float)dx * scale);
	scaled_dy = (int)((float)dy * scale);

	// Simple bounds check (full clipping would be more complex with scaling)
	if (x + scaled_dx < clipsx || x >= clipex || y + scaled_dy < clipsy || y >= clipey) {
		return;
	}

	sr.x = 0.0f;
	sr.y = 0.0f;
	sr.w = (float)dx;
	sr.h = (float)dy;

	dr.x = (float)((x + x_offset) * sdl_scale);
	dr.y = (float)((y + y_offset) * sdl_scale);
	dr.w = (float)(scaled_dx * sdl_scale);
	dr.h = (float)(scaled_dy * sdl_scale);

	if (use_gpu_rendering) {
		if (gpu_draw_is_available()) {
			gpu_draw_texture(mod_textures[tex_id].gpu_tex, &dr, &sr, mod_textures[tex_id].width,
			    mod_textures[tex_id].height, NULL, alpha);
		}
		return;
	}

	SDL_SetTextureAlphaMod(mod_textures[tex_id].tex, alpha);
	SDL_RenderTexture(sdlren, mod_textures[tex_id].tex, &sr, &dr);
}

int sdl_get_mod_texture_width(int tex_id)
{
	if (tex_id < 0 || tex_id >= MAX_MOD_TEXTURES || !mod_textures[tex_id].used) {
		return 0;
	}
	return mod_textures[tex_id].width;
}

int sdl_get_mod_texture_height(int tex_id)
{
	if (tex_id < 0 || tex_id >= MAX_MOD_TEXTURES || !mod_textures[tex_id].used) {
		return 0;
	}
	return mod_textures[tex_id].height;
}

// ============================================================================
// Render Targets for Modders
// ============================================================================

#define MAX_RENDER_TARGETS    16
#define MAX_RENDER_TARGET_DIM 4096 // Maximum dimension to prevent memory exhaustion

struct render_target {
	SDL_Texture *tex; // SDL_Renderer path
	SDL_GPUTexture *gpu_tex; // SDL_GPU path
	int width;
	int height;
	int used;
};

static struct render_target render_targets[MAX_RENDER_TARGETS];
static int render_targets_initialized = 0;
static int current_render_target = -1; // -1 = screen

static void init_render_targets(void)
{
	if (!render_targets_initialized) {
		memset(render_targets, 0, sizeof(render_targets));
		render_targets_initialized = 1;
	}
}

int sdl_create_render_target(int width, int height)
{
	SDL_Texture *tex = NULL;
	SDL_GPUTexture *gpu_tex = NULL;
	int i;

	init_render_targets();

	// Security: validate dimensions to prevent memory exhaustion
	if (width <= 0 || height <= 0) {
		warn("render target: invalid dimensions %dx%d", width, height);
		return -1;
	}
	if (width > MAX_RENDER_TARGET_DIM || height > MAX_RENDER_TARGET_DIM) {
		warn("render target: dimensions %dx%d exceed maximum %d", width, height, MAX_RENDER_TARGET_DIM);
		return -1;
	}

	// Find free slot
	for (i = 0; i < MAX_RENDER_TARGETS; i++) {
		if (!render_targets[i].used) {
			break;
		}
	}
	if (i >= MAX_RENDER_TARGETS) {
		warn("render target slots exhausted");
		return -1;
	}

	if (use_gpu_rendering) {
		// GPU path: offscreen color target (content starts undefined - use
		// sdl_clear_render_target before first use, as with SDL_Renderer)
		gpu_tex = gpu_render_target_create(width * sdl_scale, height * sdl_scale);
		if (!gpu_tex) {
			warn("failed to create GPU render target");
			return -1;
		}
	} else {
		// Create render target texture
		tex = SDL_CreateTexture(
		    sdlren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width * sdl_scale, height * sdl_scale);
		if (!tex) {
			warn("failed to create render target: %s", SDL_GetError());
			return -1;
		}

		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	}

	render_targets[i].tex = tex;
	render_targets[i].gpu_tex = gpu_tex;
	render_targets[i].width = width;
	render_targets[i].height = height;
	render_targets[i].used = 1;

	return i;
}

void sdl_destroy_render_target(int target_id)
{
	if (target_id < 0 || target_id >= MAX_RENDER_TARGETS) {
		return;
	}
	if (!render_targets[target_id].used) {
		return;
	}

	// Reset to screen if we're destroying the current target
	if (current_render_target == target_id) {
		if (use_gpu_rendering) {
			gpu_set_render_target(NULL, 0, 0, false);
		} else {
			SDL_SetRenderTarget(sdlren, NULL);
		}
		current_render_target = -1;
	}

	if (render_targets[target_id].tex) {
		SDL_DestroyTexture(render_targets[target_id].tex);
	}
	if (render_targets[target_id].gpu_tex) {
		gpu_texture_destroy(render_targets[target_id].gpu_tex);
	}
	render_targets[target_id].tex = NULL;
	render_targets[target_id].gpu_tex = NULL;
	render_targets[target_id].width = 0;
	render_targets[target_id].height = 0;
	render_targets[target_id].used = 0;
}

int sdl_set_render_target(int target_id)
{
	if (target_id < 0) {
		// Reset to screen
		if (use_gpu_rendering) {
			if (!gpu_set_render_target(NULL, 0, 0, false)) {
				return -1;
			}
		} else {
			SDL_SetRenderTarget(sdlren, NULL);
		}
		current_render_target = -1;
		return 0;
	}

	if (target_id >= MAX_RENDER_TARGETS || !render_targets[target_id].used) {
		return -1;
	}

	if (use_gpu_rendering) {
		if (!render_targets[target_id].gpu_tex ||
		    !gpu_set_render_target(render_targets[target_id].gpu_tex, render_targets[target_id].width * sdl_scale,
		        render_targets[target_id].height * sdl_scale, false)) {
			return -1;
		}
	} else {
		SDL_SetRenderTarget(sdlren, render_targets[target_id].tex);
	}
	current_render_target = target_id;
	return 0;
}

void sdl_render_target_to_screen(int target_id, int x, int y, unsigned char alpha)
{
	SDL_FRect dr;

	if (target_id < 0 || target_id >= MAX_RENDER_TARGETS) {
		return;
	}
	if (!render_targets[target_id].used) {
		return;
	}
	if (use_gpu_rendering ? !render_targets[target_id].gpu_tex : !render_targets[target_id].tex) {
		return;
	}

	dr.x = (float)(x * sdl_scale);
	dr.y = (float)(y * sdl_scale);
	dr.w = (float)(render_targets[target_id].width * sdl_scale);
	dr.h = (float)(render_targets[target_id].height * sdl_scale);

	if (use_gpu_rendering) {
		if (!gpu_draw_is_available()) {
			return;
		}
		// Make sure we're rendering to the screen
		if (current_render_target >= 0 && !gpu_set_render_target(NULL, 0, 0, false)) {
			return;
		}

		gpu_draw_texture(render_targets[target_id].gpu_tex, &dr, NULL, render_targets[target_id].width * sdl_scale,
		    render_targets[target_id].height * sdl_scale, NULL, alpha);

		// Restore previous render target
		if (current_render_target >= 0) {
			gpu_set_render_target(render_targets[current_render_target].gpu_tex,
			    render_targets[current_render_target].width * sdl_scale,
			    render_targets[current_render_target].height * sdl_scale, false);
		}
		return;
	}

	// Make sure we're rendering to the screen
	if (current_render_target >= 0) {
		SDL_SetRenderTarget(sdlren, NULL);
	}

	SDL_SetTextureAlphaMod(render_targets[target_id].tex, alpha);
	SDL_RenderTexture(sdlren, render_targets[target_id].tex, NULL, &dr);

	// Restore previous render target
	if (current_render_target >= 0) {
		SDL_SetRenderTarget(sdlren, render_targets[current_render_target].tex);
	}
}

void sdl_clear_render_target(int target_id)
{
	int prev_target = current_render_target;

	if (target_id < 0 || target_id >= MAX_RENDER_TARGETS) {
		return;
	}
	if (!render_targets[target_id].used) {
		return;
	}
	if (use_gpu_rendering ? !render_targets[target_id].gpu_tex : !render_targets[target_id].tex) {
		return;
	}

	if (use_gpu_rendering) {
		// Bind the target with a clear load-op, then restore the previous one
		if (!gpu_set_render_target(render_targets[target_id].gpu_tex, render_targets[target_id].width * sdl_scale,
		        render_targets[target_id].height * sdl_scale, true)) {
			return;
		}
		if (prev_target >= 0 && prev_target != target_id && render_targets[prev_target].gpu_tex) {
			gpu_set_render_target(render_targets[prev_target].gpu_tex, render_targets[prev_target].width * sdl_scale,
			    render_targets[prev_target].height * sdl_scale, false);
		} else if (prev_target < 0) {
			gpu_set_render_target(NULL, 0, 0, false);
		}
		return;
	}

	SDL_SetRenderTarget(sdlren, render_targets[target_id].tex);
	SDL_SetRenderDrawColor(sdlren, 0, 0, 0, 0);
	SDL_RenderClear(sdlren);

	// Restore previous target
	if (prev_target >= 0) {
		SDL_SetRenderTarget(sdlren, render_targets[prev_target].tex);
	} else {
		SDL_SetRenderTarget(sdlren, NULL);
	}
}

void sdl_render_circle(int32_t centreX, int32_t centreY, int32_t radius, uint32_t color)
{
	// GPU path draws the same midpoint-circle points as 1x1 rects below
	if (use_gpu_rendering && !gpu_draw_prim_is_available()) {
		return;
	}

// Maximum reasonable radius for screen rendering (2000 pixels)
// Formula: ((radius * 8 * 35 / 49) + (8 - 1)) & -8
// For radius=2000: ((2000 * 8 * 35 / 49) + 7) & -8 = 11428 & -8 = 11424
#define MAX_CIRCLE_PTS 11424
	SDL_FPoint pts[MAX_CIRCLE_PTS];
	int32_t pts_size = ((radius * 8 * 35 / 49) + (8 - 1)) & -8;
	if (pts_size > MAX_CIRCLE_PTS) {
		pts_size = MAX_CIRCLE_PTS;
	}
	int32_t dC = 0;

	const int32_t diameter = (radius * 2);
	int32_t x = (radius - 1);
	int32_t y = 0;
	int32_t tx = 1;
	int32_t ty = 1;
	int32_t error = (tx - diameter);

	while (x >= y) {
		if (dC + 8 > pts_size) {
			break;
		}
		pts[dC].x = (float)(centreX + x);
		pts[dC].y = (float)(centreY - y);
		dC++;
		pts[dC].x = (float)(centreX + x);
		pts[dC].y = (float)(centreY + y);
		dC++;
		pts[dC].x = (float)(centreX - x);
		pts[dC].y = (float)(centreY - y);
		dC++;
		pts[dC].x = (float)(centreX - x);
		pts[dC].y = (float)(centreY + y);
		dC++;
		pts[dC].x = (float)(centreX + y);
		pts[dC].y = (float)(centreY - x);
		dC++;
		pts[dC].x = (float)(centreX + y);
		pts[dC].y = (float)(centreY + x);
		dC++;
		pts[dC].x = (float)(centreX - y);
		pts[dC].y = (float)(centreY - x);
		dC++;
		pts[dC].x = (float)(centreX - y);
		pts[dC].y = (float)(centreY + x);
		dC++;

		if (error <= 0) {
			++y;
			error += ty;
			ty += 2;
		}

		if (error > 0) {
			--x;
			tx += 2;
			error += (tx - diameter);
		}
	}

	if (use_gpu_rendering) {
		float nr = (float)IGET_R(color) / 255.0f;
		float ng = (float)IGET_G(color) / 255.0f;
		float nb = (float)IGET_B(color) / 255.0f;
		float na = (float)IGET_A(color) / 255.0f;

		for (int32_t i = 0; i < dC; i++) {
			gpu_draw_rect(pts[i].x, pts[i].y, 1.0f, 1.0f, nr, ng, nb, na);
		}
		return;
	}

	SDL_SetRenderDrawColor(
	    sdlren, (Uint8)IGET_R(color), (Uint8)IGET_G(color), (Uint8)IGET_B(color), (Uint8)IGET_A(color));
	SDL_RenderPoints(sdlren, pts, (int)dC);
}

// ============================================================================
// Extended Rendering Primitives for Modders
// ============================================================================

void sdl_circle_alpha(int cx, int cy, int radius, unsigned short color, unsigned char alpha, int x_offset, int y_offset)
{
	int r, g, b;
	int x, y, d;

	if (radius <= 0) {
		return;
	}

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	// GPU path: use pixels (small rectangles) for circle outline
	if (use_gpu_rendering && gpu_draw_prim_is_available()) {
		float nr = (float)r / 255.0f;
		float ng = (float)g / 255.0f;
		float nb = (float)b / 255.0f;
		float na = (float)alpha / 255.0f;

		// Scale center and radius
		int scx = (cx + x_offset) * sdl_scale;
		int scy = (cy + y_offset) * sdl_scale;
		int sr = radius * sdl_scale;

		// Midpoint circle algorithm
		x = sr;
		y = 0;
		d = 1 - sr;

		while (x >= y) {
			// Draw 8 symmetric points as small rectangles
			gpu_draw_rect((float)(scx + x), (float)(scy + y), 1.0f, 1.0f, nr, ng, nb, na);
			gpu_draw_rect((float)(scx - x), (float)(scy + y), 1.0f, 1.0f, nr, ng, nb, na);
			gpu_draw_rect((float)(scx + x), (float)(scy - y), 1.0f, 1.0f, nr, ng, nb, na);
			gpu_draw_rect((float)(scx - x), (float)(scy - y), 1.0f, 1.0f, nr, ng, nb, na);
			gpu_draw_rect((float)(scx + y), (float)(scy + x), 1.0f, 1.0f, nr, ng, nb, na);
			gpu_draw_rect((float)(scx - y), (float)(scy + x), 1.0f, 1.0f, nr, ng, nb, na);
			gpu_draw_rect((float)(scx + y), (float)(scy - x), 1.0f, 1.0f, nr, ng, nb, na);
			gpu_draw_rect((float)(scx - y), (float)(scy - x), 1.0f, 1.0f, nr, ng, nb, na);

			y++;
			if (d < 0) {
				d += 2 * y + 1;
			} else {
				x--;
				d += 2 * (y - x) + 1;
			}
		}
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	// Scale center and radius
	cx = (cx + x_offset) * sdl_scale;
	cy = (cy + y_offset) * sdl_scale;
	int sr = radius * sdl_scale;

	// Batch render: collect all points first, then render in one call
	// Max points: 8 per iteration, iterations ≈ radius * 0.71, so max ≈ radius * 6
	// Use same formula as sdl_render_circle for consistency
	int max_pts = ((sr * 8 * 35 / 49) + 7) & ~7;
	if (max_pts < 64) {
		max_pts = 64;
	}
	if (max_pts > MAX_CIRCLE_PTS) {
		max_pts = MAX_CIRCLE_PTS;
	}

	SDL_FPoint pts[MAX_CIRCLE_PTS];
	int pt_count = 0;

	// Midpoint circle algorithm using scaled radius
	x = sr;
	y = 0;
	d = 1 - sr;

	while (x >= y) {
		if (pt_count + 8 > max_pts) {
			break;
		}

		// Collect 8 symmetric points
		pts[pt_count++] = (SDL_FPoint){(float)(cx + x), (float)(cy + y)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx - x), (float)(cy + y)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx + x), (float)(cy - y)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx - x), (float)(cy - y)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx + y), (float)(cy + x)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx - y), (float)(cy + x)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx + y), (float)(cy - x)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx - y), (float)(cy - x)};

		y++;
		if (d < 0) {
			d += 2 * y + 1;
		} else {
			x--;
			d += 2 * (y - x) + 1;
		}
	}

	// Single batch render call
	if (pt_count > 0) {
		SDL_RenderPoints(sdlren, pts, pt_count);
	}
}

void sdl_circle_filled_alpha(
    int cx, int cy, int radius, unsigned short color, unsigned char alpha, int x_offset, int y_offset)
{
	if (radius <= 0) {
		return;
	}

	// SDL3 SDL_Vertex uses SDL_FColor with float values 0.0-1.0
	float fr = (float)R16TO32(color) / 255.0f;
	float fg = (float)G16TO32(color) / 255.0f;
	float fb = (float)B16TO32(color) / 255.0f;
	float fa = (float)alpha / 255.0f;

	// GPU path: use horizontal rectangles (scanlines) for filled circle
	if (use_gpu_rendering && gpu_draw_prim_is_available()) {
		float nr = fr;
		float ng = fg;
		float nb = fb;
		float na = fa;

		// Scale center and radius
		int scx = (cx + x_offset) * sdl_scale;
		int scy = (cy + y_offset) * sdl_scale;
		int sr = radius * sdl_scale;

		// Midpoint circle algorithm to draw horizontal scanlines
		int x = sr;
		int y = 0;
		int d = 1 - sr;
		int prev_x = x + 1; // Track to avoid duplicate lines

		while (x >= y) {
			// Draw horizontal lines using midpoint circle symmetry
			// Only draw when x changes to avoid overdraw
			if (x != prev_x) {
				// Horizontal line at y offset from center (top half)
				gpu_draw_rect((float)(scx - x), (float)(scy - y), (float)(2 * x + 1), 1.0f, nr, ng, nb, na);
				// Horizontal line at -y offset from center (bottom half)
				if (y != 0) {
					gpu_draw_rect((float)(scx - x), (float)(scy + y), (float)(2 * x + 1), 1.0f, nr, ng, nb, na);
				}
			}

			// Draw lines at x offset from center (swapped coordinates)
			// Horizontal line at x offset from center
			gpu_draw_rect((float)(scx - y), (float)(scy - x), (float)(2 * y + 1), 1.0f, nr, ng, nb, na);
			if (x != 0) {
				gpu_draw_rect((float)(scx - y), (float)(scy + x), (float)(2 * y + 1), 1.0f, nr, ng, nb, na);
			}

			prev_x = x;
			y++;
			if (d < 0) {
				d += 2 * y + 1;
			} else {
				x--;
				d += 2 * (y - x) + 1;
			}
		}
		return;
	}

	float fcx = (float)((cx + x_offset) * sdl_scale);
	float fcy = (float)((cy + y_offset) * sdl_scale);
	float fsr = (float)(radius * sdl_scale);

// Use SDL_RenderGeometry with triangle fan for GPU-accelerated filled circle
// 72 segments (every 5 degrees) provides smooth circles while keeping vertex count low
#define CIRCLE_SEGMENTS 72
	SDL_Vertex vertices[CIRCLE_SEGMENTS + 2]; // center + perimeter + closing vertex
	int indices[CIRCLE_SEGMENTS * 3];

	// Center vertex
	vertices[0] = (SDL_Vertex){{fcx, fcy}, {fr, fg, fb, fa}, {0, 0}};

	// Perimeter vertices
	for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
		float angle = (float)i * (2.0f * (float)M_PI / (float)CIRCLE_SEGMENTS);
		vertices[i + 1] = (SDL_Vertex){{fcx + fsr * cosf(angle), fcy + fsr * sinf(angle)}, {fr, fg, fb, fa}, {0, 0}};
	}

	// Triangle fan indices (center to each adjacent pair of perimeter vertices)
	for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
		indices[i * 3] = 0; // Center
		indices[i * 3 + 1] = i + 1; // Current perimeter vertex
		indices[i * 3 + 2] = i + 2; // Next perimeter vertex
	}

	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);
	SDL_RenderGeometry(sdlren, NULL, vertices, CIRCLE_SEGMENTS + 2, indices, CIRCLE_SEGMENTS * 3);
#undef CIRCLE_SEGMENTS
}

void sdl_ellipse_alpha(
    int cx, int cy, int rx, int ry, unsigned short color, unsigned char alpha, int x_offset, int y_offset)
{
	int r, g, b;

	if (rx <= 0 || ry <= 0) {
		return;
	}

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	// GPU path: use pixels (small rectangles) for ellipse outline
	if (use_gpu_rendering && gpu_draw_prim_is_available()) {
		float nr = (float)r / 255.0f;
		float ng = (float)g / 255.0f;
		float nb = (float)b / 255.0f;
		float na = (float)alpha / 255.0f;

		int scx = (cx + x_offset) * sdl_scale;
		int scy = (cy + y_offset) * sdl_scale;
		int srx = rx * sdl_scale;
		int sry = ry * sdl_scale;

		// Draw ellipse using parametric approach
		int segments = 72; // Match SDL_Renderer version

		for (int i = 0; i < segments; i++) {
			float angle = (float)i * (2.0f * (float)M_PI / (float)segments);
			float px = (float)scx + (float)srx * cosf(angle);
			float py = (float)scy + (float)sry * sinf(angle);
			// Draw point at current position
			gpu_draw_rect(px, py, 1.0f, 1.0f, nr, ng, nb, na);
		}
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	cx = (cx + x_offset) * sdl_scale;
	cy = (cy + y_offset) * sdl_scale;
	rx *= sdl_scale;
	ry *= sdl_scale;

	// Batch render: collect all points, render once
	// Ellipse perimeter ≈ π * (3(rx+ry) - sqrt((3rx+ry)(rx+3ry))) ≈ 2π * max(rx,ry)
	// We draw 4 points per iteration, so max points ≈ 4 * (rx + ry)
	int max_pts = 4 * (rx + ry + 2);
	if (max_pts > MAX_CIRCLE_PTS) {
		max_pts = MAX_CIRCLE_PTS;
	}

	SDL_FPoint pts[MAX_CIRCLE_PTS];
	int pt_count = 0;

	// Midpoint ellipse algorithm
	int x = 0, y = ry;
	long rx2 = (long)rx * rx;
	long ry2 = (long)ry * ry;
	long two_rx2 = 2 * rx2;
	long two_ry2 = 2 * ry2;
	long px = 0;
	long py = two_rx2 * y;
	long p;

	// Region 1
	p = ry2 - rx2 * ry + rx2 / 4;
	while (px < py) {
		if (pt_count + 4 > max_pts) {
			break;
		}
		pts[pt_count++] = (SDL_FPoint){(float)(cx + x), (float)(cy + y)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx - x), (float)(cy + y)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx + x), (float)(cy - y)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx - x), (float)(cy - y)};

		x++;
		px += two_ry2;
		if (p < 0) {
			p += ry2 + px;
		} else {
			y--;
			py -= two_rx2;
			p += ry2 + px - py;
		}
	}

	// Region 2
	p = ry2 * (x * 2 + 1) * (x * 2 + 1) / 4 + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
	while (y >= 0) {
		if (pt_count + 4 > max_pts) {
			break;
		}
		pts[pt_count++] = (SDL_FPoint){(float)(cx + x), (float)(cy + y)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx - x), (float)(cy + y)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx + x), (float)(cy - y)};
		pts[pt_count++] = (SDL_FPoint){(float)(cx - x), (float)(cy - y)};

		y--;
		py -= two_rx2;
		if (p > 0) {
			p += rx2 - py;
		} else {
			x++;
			px += two_ry2;
			p += rx2 - py + px;
		}
	}

	// Single batch render call
	if (pt_count > 0) {
		SDL_RenderPoints(sdlren, pts, pt_count);
	}
}

void sdl_ellipse_filled_alpha(
    int cx, int cy, int rx, int ry, unsigned short color, unsigned char alpha, int x_offset, int y_offset)
{
	if (rx <= 0 || ry <= 0) {
		return;
	}

	// SDL3 SDL_Vertex uses SDL_FColor with float values 0.0-1.0
	float fr = (float)R16TO32(color) / 255.0f;
	float fg = (float)G16TO32(color) / 255.0f;
	float fb = (float)B16TO32(color) / 255.0f;
	float fa = (float)alpha / 255.0f;

	// GPU path: use horizontal rectangles for filled ellipse
	if (use_gpu_rendering && gpu_draw_prim_is_available()) {
		float nr = fr;
		float ng = fg;
		float nb = fb;
		float na = fa;

		int scx = (cx + x_offset) * sdl_scale;
		int scy = (cy + y_offset) * sdl_scale;
		int srx = rx * sdl_scale;
		int sry = ry * sdl_scale;

		// Draw horizontal scanlines for each row
		for (int dy = -sry; dy <= sry; dy++) {
			// Calculate x extent at this y using ellipse equation
			// x^2/rx^2 + y^2/ry^2 = 1  =>  x = rx * sqrt(1 - y^2/ry^2)
			float t = 1.0f - ((float)(dy * dy)) / ((float)(sry * sry));
			if (t < 0) {
				t = 0;
			}
			int dx = (int)((float)srx * sqrtf(t));
			if (dx > 0) {
				gpu_draw_rect((float)(scx - dx), (float)(scy + dy), (float)(2 * dx + 1), 1.0f, nr, ng, nb, na);
			}
		}
		return;
	}

	float fcx = (float)((cx + x_offset) * sdl_scale);
	float fcy = (float)((cy + y_offset) * sdl_scale);
	float frx = (float)(rx * sdl_scale);
	float fry = (float)(ry * sdl_scale);

// Use SDL_RenderGeometry with triangle fan for GPU-accelerated filled ellipse
#define ELLIPSE_SEGMENTS 72
	SDL_Vertex vertices[ELLIPSE_SEGMENTS + 2];
	int indices[ELLIPSE_SEGMENTS * 3];

	// Center vertex
	vertices[0] = (SDL_Vertex){{fcx, fcy}, {fr, fg, fb, fa}, {0, 0}};

	// Perimeter vertices (ellipse uses different x and y radii)
	for (int i = 0; i <= ELLIPSE_SEGMENTS; i++) {
		float angle = (float)i * (2.0f * (float)M_PI / (float)ELLIPSE_SEGMENTS);
		vertices[i + 1] = (SDL_Vertex){{fcx + frx * cosf(angle), fcy + fry * sinf(angle)}, {fr, fg, fb, fa}, {0, 0}};
	}

	// Triangle fan indices
	for (int i = 0; i < ELLIPSE_SEGMENTS; i++) {
		indices[i * 3] = 0;
		indices[i * 3 + 1] = i + 1;
		indices[i * 3 + 2] = i + 2;
	}

	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);
	SDL_RenderGeometry(sdlren, NULL, vertices, ELLIPSE_SEGMENTS + 2, indices, ELLIPSE_SEGMENTS * 3);
#undef ELLIPSE_SEGMENTS
}

void sdl_rect_outline_alpha(int sx, int sy, int ex, int ey, unsigned short color, unsigned char alpha, int clipsx,
    int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	int r, g, b;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	// Apply clipping
	if (sx < clipsx) {
		sx = clipsx;
	}
	if (sy < clipsy) {
		sy = clipsy;
	}
	if (ex > clipex) {
		ex = clipex;
	}
	if (ey > clipey) {
		ey = clipey;
	}

	if (sx >= ex || sy >= ey) {
		return;
	}

	// GPU path: use GPU lines for rectangle outline
	if (use_gpu_rendering && gpu_draw_line_is_available()) {
		float fsx = (float)((sx + x_offset) * sdl_scale);
		float fsy = (float)((sy + y_offset) * sdl_scale);
		float fex = (float)((ex + x_offset) * sdl_scale - 1);
		float fey = (float)((ey + y_offset) * sdl_scale - 1);
		float nr = (float)r / 255.0f;
		float ng = (float)g / 255.0f;
		float nb = (float)b / 255.0f;
		float na = (float)alpha / 255.0f;

		// Draw 4 lines for rectangle outline
		gpu_draw_line(fsx, fsy, fex, fsy, nr, ng, nb, na); // Top
		gpu_draw_line(fex, fsy, fex, fey, nr, ng, nb, na); // Right
		gpu_draw_line(fex, fey, fsx, fey, nr, ng, nb, na); // Bottom
		gpu_draw_line(fsx, fey, fsx, fsy, nr, ng, nb, na); // Left
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	float fsx = (float)((sx + x_offset) * sdl_scale);
	float fsy = (float)((sy + y_offset) * sdl_scale);
	float fex = (float)((ex + x_offset) * sdl_scale - 1);
	float fey = (float)((ey + y_offset) * sdl_scale - 1);

	// Draw closed rectangle outline with single batch call (5 points for closed loop)
	SDL_FPoint pts[5] = {
	    {fsx, fsy}, // Top-left
	    {fex, fsy}, // Top-right
	    {fex, fey}, // Bottom-right
	    {fsx, fey}, // Bottom-left
	    {fsx, fsy} // Back to top-left (close the loop)
	};
	SDL_RenderLines(sdlren, pts, 5);
}

void sdl_rounded_rect_alpha(int sx, int sy, int ex, int ey, int radius, unsigned short color, unsigned char alpha,
    int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	int r, g, b;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	// Apply clipping
	if (sx < clipsx) {
		sx = clipsx;
	}
	if (sy < clipsy) {
		sy = clipsy;
	}
	if (ex > clipex) {
		ex = clipex;
	}
	if (ey > clipey) {
		ey = clipey;
	}

	if (sx >= ex || sy >= ey) {
		return;
	}

	// Limit radius to half the smallest dimension
	int max_radius = ((ex - sx) < (ey - sy) ? (ex - sx) : (ey - sy)) / 2;
	if (radius > max_radius) {
		radius = max_radius;
	}
	if (radius < 0) {
		radius = 0;
	}

	// GPU path: same geometry as below - straight edges as 1px rects, corner
	// arcs as midpoint-circle points (1x1 rects)
	if (use_gpu_rendering) {
		if (!gpu_draw_prim_is_available()) {
			return;
		}

		float nr = (float)r / 255.0f;
		float ng = (float)g / 255.0f;
		float nb = (float)b / 255.0f;
		float na = (float)alpha / 255.0f;

		int osx = (sx + x_offset) * sdl_scale;
		int osy = (sy + y_offset) * sdl_scale;
		int oex = (ex + x_offset) * sdl_scale;
		int oey = (ey + y_offset) * sdl_scale;
		int sr = radius * sdl_scale;

		// The 4 straight edges (SDL_RenderLine endpoints are inclusive)
		int edge_w = (oex - sr - 1) - (osx + sr) + 1;
		int edge_h = (oey - sr - 1) - (osy + sr) + 1;
		if (edge_w > 0) {
			gpu_draw_rect((float)(osx + sr), (float)osy, (float)edge_w, 1.0f, nr, ng, nb, na); // Top
			gpu_draw_rect((float)(osx + sr), (float)(oey - 1), (float)edge_w, 1.0f, nr, ng, nb, na); // Bottom
		}
		if (edge_h > 0) {
			gpu_draw_rect((float)osx, (float)(osy + sr), 1.0f, (float)edge_h, nr, ng, nb, na); // Left
			gpu_draw_rect((float)(oex - 1), (float)(osy + sr), 1.0f, (float)edge_h, nr, ng, nb, na); // Right
		}

		// The 4 corner arcs (midpoint circle algorithm)
		if (sr > 0) {
			int x = sr, y = 0, d = 1 - sr;
			int cx1 = osx + sr, cy1 = osy + sr; // Top-left
			int cx2 = oex - sr - 1, cy2 = osy + sr; // Top-right
			int cx3 = osx + sr, cy3 = oey - sr - 1; // Bottom-left
			int cx4 = oex - sr - 1, cy4 = oey - sr - 1; // Bottom-right

			while (x >= y) {
				gpu_draw_rect((float)(cx1 - x), (float)(cy1 - y), 1.0f, 1.0f, nr, ng, nb, na);
				gpu_draw_rect((float)(cx1 - y), (float)(cy1 - x), 1.0f, 1.0f, nr, ng, nb, na);
				gpu_draw_rect((float)(cx2 + x), (float)(cy2 - y), 1.0f, 1.0f, nr, ng, nb, na);
				gpu_draw_rect((float)(cx2 + y), (float)(cy2 - x), 1.0f, 1.0f, nr, ng, nb, na);
				gpu_draw_rect((float)(cx3 - x), (float)(cy3 + y), 1.0f, 1.0f, nr, ng, nb, na);
				gpu_draw_rect((float)(cx3 - y), (float)(cy3 + x), 1.0f, 1.0f, nr, ng, nb, na);
				gpu_draw_rect((float)(cx4 + x), (float)(cy4 + y), 1.0f, 1.0f, nr, ng, nb, na);
				gpu_draw_rect((float)(cx4 + y), (float)(cy4 + x), 1.0f, 1.0f, nr, ng, nb, na);

				y++;
				if (d < 0) {
					d += 2 * y + 1;
				} else {
					x--;
					d += 2 * (y - x) + 1;
				}
			}
		}
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	int osx = (sx + x_offset) * sdl_scale;
	int osy = (sy + y_offset) * sdl_scale;
	int oex = (ex + x_offset) * sdl_scale;
	int oey = (ey + y_offset) * sdl_scale;
	int sr = radius * sdl_scale;

	// Draw the 4 straight edges
	SDL_RenderLine(sdlren, (float)(osx + sr), (float)osy, (float)(oex - sr - 1), (float)osy); // Top
	SDL_RenderLine(sdlren, (float)(osx + sr), (float)(oey - 1), (float)(oex - sr - 1), (float)(oey - 1)); // Bottom
	SDL_RenderLine(sdlren, (float)osx, (float)(osy + sr), (float)osx, (float)(oey - sr - 1)); // Left
	SDL_RenderLine(sdlren, (float)(oex - 1), (float)(osy + sr), (float)(oex - 1), (float)(oey - sr - 1)); // Right

	// Draw the 4 corner arcs using midpoint circle algorithm
	if (sr > 0) {
		int x = sr, y = 0, d = 1 - sr;
		int cx1 = osx + sr, cy1 = osy + sr; // Top-left
		int cx2 = oex - sr - 1, cy2 = osy + sr; // Top-right
		int cx3 = osx + sr, cy3 = oey - sr - 1; // Bottom-left
		int cx4 = oex - sr - 1, cy4 = oey - sr - 1; // Bottom-right

		while (x >= y) {
			// Top-left corner
			SDL_RenderPoint(sdlren, (float)(cx1 - x), (float)(cy1 - y));
			SDL_RenderPoint(sdlren, (float)(cx1 - y), (float)(cy1 - x));
			// Top-right corner
			SDL_RenderPoint(sdlren, (float)(cx2 + x), (float)(cy2 - y));
			SDL_RenderPoint(sdlren, (float)(cx2 + y), (float)(cy2 - x));
			// Bottom-left corner
			SDL_RenderPoint(sdlren, (float)(cx3 - x), (float)(cy3 + y));
			SDL_RenderPoint(sdlren, (float)(cx3 - y), (float)(cy3 + x));
			// Bottom-right corner
			SDL_RenderPoint(sdlren, (float)(cx4 + x), (float)(cy4 + y));
			SDL_RenderPoint(sdlren, (float)(cx4 + y), (float)(cy4 + x));

			y++;
			if (d < 0) {
				d += 2 * y + 1;
			} else {
				x--;
				d += 2 * (y - x) + 1;
			}
		}
	}
}

void sdl_rounded_rect_filled_alpha(int sx, int sy, int ex, int ey, int radius, unsigned short color,
    unsigned char alpha, int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	int r, g, b;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	// Apply clipping
	if (sx < clipsx) {
		sx = clipsx;
	}
	if (sy < clipsy) {
		sy = clipsy;
	}
	if (ex > clipex) {
		ex = clipex;
	}
	if (ey > clipey) {
		ey = clipey;
	}

	if (sx >= ex || sy >= ey) {
		return;
	}

	// Limit radius to half the smallest dimension
	int max_radius = ((ex - sx) < (ey - sy) ? (ex - sx) : (ey - sy)) / 2;
	if (radius > max_radius) {
		radius = max_radius;
	}
	if (radius < 0) {
		radius = 0;
	}

	// GPU path: same decomposition as below - three fill rects plus corner
	// quadrants as 1px-high scanline rects (used by mod widget panel
	// backgrounds: ChatTabs window, menu-bar sidebar, weather indicator, ...)
	if (use_gpu_rendering) {
		if (!gpu_draw_prim_is_available()) {
			return;
		}

		float nr = (float)r / 255.0f;
		float ng = (float)g / 255.0f;
		float nb = (float)b / 255.0f;
		float na = (float)alpha / 255.0f;

		int osx = (sx + x_offset) * sdl_scale;
		int osy = (sy + y_offset) * sdl_scale;
		int oex = (ex + x_offset) * sdl_scale;
		int oey = (ey + y_offset) * sdl_scale;
		int sr = radius * sdl_scale;

		// Fill center rectangle
		gpu_draw_rect((float)osx, (float)(osy + sr), (float)(oex - osx), (float)(oey - osy - 2 * sr), nr, ng, nb, na);

		// Fill top and bottom rectangles (between corners)
		if (sr > 0 && oex - osx - 2 * sr > 0) {
			gpu_draw_rect((float)(osx + sr), (float)osy, (float)(oex - osx - 2 * sr), (float)sr, nr, ng, nb, na);
			gpu_draw_rect((float)(osx + sr), (float)(oey - sr), (float)(oex - osx - 2 * sr), (float)sr, nr, ng, nb, na);
		}

		// Fill the 4 corners with circle quadrants (horizontal scanlines,
		// SDL_RenderLine endpoints are inclusive -> width x+1 / y+1)
		if (sr > 0) {
			int x = sr, y = 0, d = 1 - sr;
			int cx1 = osx + sr, cy1 = osy + sr;
			int cx2 = oex - sr - 1, cy2 = osy + sr;
			int cx3 = osx + sr, cy3 = oey - sr - 1;
			int cx4 = oex - sr - 1, cy4 = oey - sr - 1;

			while (x >= y) {
				gpu_draw_rect((float)(cx1 - x), (float)(cy1 - y), (float)(x + 1), 1.0f, nr, ng, nb, na); // Top-left
				gpu_draw_rect((float)(cx1 - y), (float)(cy1 - x), (float)(y + 1), 1.0f, nr, ng, nb, na);
				gpu_draw_rect((float)cx2, (float)(cy2 - y), (float)(x + 1), 1.0f, nr, ng, nb, na); // Top-right
				gpu_draw_rect((float)cx2, (float)(cy2 - x), (float)(y + 1), 1.0f, nr, ng, nb, na);
				gpu_draw_rect((float)(cx3 - x), (float)(cy3 + y), (float)(x + 1), 1.0f, nr, ng, nb, na); // Bottom-left
				gpu_draw_rect((float)(cx3 - y), (float)(cy3 + x), (float)(y + 1), 1.0f, nr, ng, nb, na);
				gpu_draw_rect((float)cx4, (float)(cy4 + y), (float)(x + 1), 1.0f, nr, ng, nb, na); // Bottom-right
				gpu_draw_rect((float)cx4, (float)(cy4 + x), (float)(y + 1), 1.0f, nr, ng, nb, na);

				y++;
				if (d < 0) {
					d += 2 * y + 1;
				} else {
					x--;
					d += 2 * (y - x) + 1;
				}
			}
		}
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	int osx = (sx + x_offset) * sdl_scale;
	int osy = (sy + y_offset) * sdl_scale;
	int oex = (ex + x_offset) * sdl_scale;
	int oey = (ey + y_offset) * sdl_scale;
	int sr = radius * sdl_scale;

	// Fill center rectangle
	SDL_FRect center = {(float)osx, (float)(osy + sr), (float)(oex - osx), (float)(oey - osy - 2 * sr)};
	SDL_RenderFillRect(sdlren, &center);

	// Fill top and bottom rectangles (between corners)
	SDL_FRect top = {(float)(osx + sr), (float)osy, (float)(oex - osx - 2 * sr), (float)sr};
	SDL_FRect bottom = {(float)(osx + sr), (float)(oey - sr), (float)(oex - osx - 2 * sr), (float)sr};
	SDL_RenderFillRect(sdlren, &top);
	SDL_RenderFillRect(sdlren, &bottom);

	// Fill the 4 corners with circle quadrants
	if (sr > 0) {
		int x = sr, y = 0, d = 1 - sr;
		int cx1 = osx + sr, cy1 = osy + sr;
		int cx2 = oex - sr - 1, cy2 = osy + sr;
		int cx3 = osx + sr, cy3 = oey - sr - 1;
		int cx4 = oex - sr - 1, cy4 = oey - sr - 1;

		while (x >= y) {
			// Fill horizontal lines for each corner
			SDL_RenderLine(sdlren, (float)(cx1 - x), (float)(cy1 - y), (float)cx1, (float)(cy1 - y)); // Top-left
			SDL_RenderLine(sdlren, (float)(cx1 - y), (float)(cy1 - x), (float)cx1, (float)(cy1 - x));
			SDL_RenderLine(sdlren, (float)cx2, (float)(cy2 - y), (float)(cx2 + x), (float)(cy2 - y)); // Top-right
			SDL_RenderLine(sdlren, (float)cx2, (float)(cy2 - x), (float)(cx2 + y), (float)(cy2 - x));
			SDL_RenderLine(sdlren, (float)(cx3 - x), (float)(cy3 + y), (float)cx3, (float)(cy3 + y)); // Bottom-left
			SDL_RenderLine(sdlren, (float)(cx3 - y), (float)(cy3 + x), (float)cx3, (float)(cy3 + x));
			SDL_RenderLine(sdlren, (float)cx4, (float)(cy4 + y), (float)(cx4 + x), (float)(cy4 + y)); // Bottom-right
			SDL_RenderLine(sdlren, (float)cx4, (float)(cy4 + x), (float)(cx4 + y), (float)(cy4 + x));

			y++;
			if (d < 0) {
				d += 2 * y + 1;
			} else {
				x--;
				d += 2 * (y - x) + 1;
			}
		}
	}
}

void sdl_triangle_alpha(int x1, int y1, int x2, int y2, int x3, int y3, unsigned short color, unsigned char alpha,
    int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	int r, g, b;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	// Apply clipping (simple bounds check)
	int minx = (x1 < x2 ? (x1 < x3 ? x1 : x3) : (x2 < x3 ? x2 : x3));
	int maxx = (x1 > x2 ? (x1 > x3 ? x1 : x3) : (x2 > x3 ? x2 : x3));
	int miny = (y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3));
	int maxy = (y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3));

	if (maxx < clipsx || minx >= clipex || maxy < clipsy || miny >= clipey) {
		return;
	}

	x1 = (x1 + x_offset) * sdl_scale;
	y1 = (y1 + y_offset) * sdl_scale;
	x2 = (x2 + x_offset) * sdl_scale;
	y2 = (y2 + y_offset) * sdl_scale;
	x3 = (x3 + x_offset) * sdl_scale;
	y3 = (y3 + y_offset) * sdl_scale;

	// GPU path: 3 GPU lines
	if (use_gpu_rendering) {
		if (!gpu_draw_line_is_available()) {
			return;
		}
		float nr = (float)r / 255.0f;
		float ng = (float)g / 255.0f;
		float nb = (float)b / 255.0f;
		float na = (float)alpha / 255.0f;

		gpu_draw_line((float)x1, (float)y1, (float)x2, (float)y2, nr, ng, nb, na);
		gpu_draw_line((float)x2, (float)y2, (float)x3, (float)y3, nr, ng, nb, na);
		gpu_draw_line((float)x3, (float)y3, (float)x1, (float)y1, nr, ng, nb, na);
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	// Draw 3 lines
	SDL_RenderLine(sdlren, (float)x1, (float)y1, (float)x2, (float)y2);
	SDL_RenderLine(sdlren, (float)x2, (float)y2, (float)x3, (float)y3);
	SDL_RenderLine(sdlren, (float)x3, (float)y3, (float)x1, (float)y1);
}

void sdl_triangle_filled_alpha(int x1, int y1, int x2, int y2, int x3, int y3, unsigned short color,
    unsigned char alpha, int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	int r, g, b;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	// Apply clipping (simple bounds check)
	int minx = (x1 < x2 ? (x1 < x3 ? x1 : x3) : (x2 < x3 ? x2 : x3));
	int maxx = (x1 > x2 ? (x1 > x3 ? x1 : x3) : (x2 > x3 ? x2 : x3));
	int miny = (y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3));
	int maxy = (y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3));

	if (maxx < clipsx || minx >= clipex || maxy < clipsy || miny >= clipey) {
		return;
	}

	// GPU path: one solid GPU triangle
	if (use_gpu_rendering) {
		if (!gpu_draw_tri_is_available()) {
			return;
		}
		float c[4] = {(float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, (float)alpha / 255.0f};

		gpu_draw_triangle((float)((x1 + x_offset) * sdl_scale), (float)((y1 + y_offset) * sdl_scale),
		    (float)((x2 + x_offset) * sdl_scale), (float)((y2 + y_offset) * sdl_scale),
		    (float)((x3 + x_offset) * sdl_scale), (float)((y3 + y_offset) * sdl_scale), c, NULL, NULL);
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	x1 = (x1 + x_offset) * sdl_scale;
	y1 = (y1 + y_offset) * sdl_scale;
	x2 = (x2 + x_offset) * sdl_scale;
	y2 = (y2 + y_offset) * sdl_scale;
	x3 = (x3 + x_offset) * sdl_scale;
	y3 = (y3 + y_offset) * sdl_scale;

	// Sort vertices by y coordinate
	int tmp;
	if (y1 > y2) {
		tmp = y1;
		y1 = y2;
		y2 = tmp;
		tmp = x1;
		x1 = x2;
		x2 = tmp;
	}
	if (y1 > y3) {
		tmp = y1;
		y1 = y3;
		y3 = tmp;
		tmp = x1;
		x1 = x3;
		x3 = tmp;
	}
	if (y2 > y3) {
		tmp = y2;
		y2 = y3;
		y3 = tmp;
		tmp = x2;
		x2 = x3;
		x3 = tmp;
	}

	// Scanline fill
	int total_height = y3 - y1;
	if (total_height == 0) {
		return;
	}

	for (int y = y1; y <= y3; y++) {
		int second_half = (y > y2) || (y2 == y1);
		int segment_height = second_half ? (y3 - y2) : (y2 - y1);
		if (segment_height == 0) {
			segment_height = 1;
		}

		float alpha_val = (float)(y - y1) / (float)total_height;
		float beta = second_half ? (float)(y - y2) / (float)segment_height : (float)(y - y1) / (float)segment_height;

		int xa = x1 + (int)((float)(x3 - x1) * alpha_val);
		int xb = second_half ? x2 + (int)((float)(x3 - x2) * beta) : x1 + (int)((float)(x2 - x1) * beta);

		if (xa > xb) {
			tmp = xa;
			xa = xb;
			xb = tmp;
		}

		SDL_RenderLine(sdlren, (float)xa, (float)y, (float)xb, (float)y);
	}
}

void sdl_thick_line_alpha(int fx, int fy, int tx, int ty, int thickness, unsigned short color, unsigned char alpha,
    int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	if (thickness <= 0) {
		thickness = 1;
	}

	// Clip the center line first (preserves slope)
	if (!clip_line(&fx, &fy, &tx, &ty, clipsx, clipsy, clipex - 1, clipey - 1)) {
		// Line is completely outside clip rect
		return;
	}

	// SDL3 SDL_Vertex uses SDL_FColor with float values 0.0-1.0
	float fr = (float)R16TO32(color) / 255.0f;
	float fg = (float)G16TO32(color) / 255.0f;
	float fb = (float)B16TO32(color) / 255.0f;
	float fa = (float)alpha / 255.0f;

	// Apply offset and scale
	float ffx = (float)((fx + x_offset) * sdl_scale);
	float ffy = (float)((fy + y_offset) * sdl_scale);
	float ftx = (float)((tx + x_offset) * sdl_scale);
	float fty = (float)((ty + y_offset) * sdl_scale);
	float half_thick = (float)(thickness * sdl_scale) / 2.0f;

	// Calculate perpendicular vector
	float dx = ftx - ffx;
	float dy = fty - ffy;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.001f) {
		return;
	}

	// Perpendicular unit vector scaled by half thickness
	float nx = (-dy / len) * half_thick;
	float ny = (dx / len) * half_thick;

	// GPU path: the same quad as two solid GPU triangles
	if (use_gpu_rendering) {
		if (!gpu_draw_tri_is_available()) {
			return;
		}
		float c[4] = {fr, fg, fb, fa};

		gpu_draw_triangle(ffx + nx, ffy + ny, ffx - nx, ffy - ny, ftx - nx, fty - ny, c, NULL, NULL);
		gpu_draw_triangle(ffx + nx, ffy + ny, ftx - nx, fty - ny, ftx + nx, fty + ny, c, NULL, NULL);
		return;
	}

	// Use SDL_RenderGeometry for GPU-accelerated thick line (single draw call)
	// Construct a quad from 4 corner points
	SDL_Vertex vertices[4] = {
	    {{ffx + nx, ffy + ny}, {fr, fg, fb, fa}, {0, 0}}, // Start + perpendicular
	    {{ffx - nx, ffy - ny}, {fr, fg, fb, fa}, {0, 0}}, // Start - perpendicular
	    {{ftx - nx, fty - ny}, {fr, fg, fb, fa}, {0, 0}}, // End - perpendicular
	    {{ftx + nx, fty + ny}, {fr, fg, fb, fa}, {0, 0}} // End + perpendicular
	};

	// Two triangles to form the quad
	int indices[6] = {0, 1, 2, 0, 2, 3};

	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);
	SDL_RenderGeometry(sdlren, NULL, vertices, 4, indices, 6);
}

void sdl_arc_alpha(int cx, int cy, int radius, int start_angle, int end_angle, unsigned short color,
    unsigned char alpha, int x_offset, int y_offset)
{
	int r, g, b;

	if (radius <= 0) {
		return;
	}

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	cx = (cx + x_offset) * sdl_scale;
	cy = (cy + y_offset) * sdl_scale;
	int sr = radius * sdl_scale;

	// Normalize angles
	while (start_angle < 0) {
		start_angle += 360;
	}
	while (end_angle < 0) {
		end_angle += 360;
	}
	start_angle %= 360;
	end_angle %= 360;

	// Batch render: collect all points, render once (max 361 points for full circle)
	SDL_FPoint pts[362];
	int pt_count = 0;

	// Collect arc points
	for (int angle = start_angle; angle != end_angle && pt_count < 361; angle = (angle + 1) % 360) {
		double rad = angle * M_PI / 180.0;
		pts[pt_count++] = (SDL_FPoint){(float)(cx + (int)(sr * cos(rad))), (float)(cy + (int)(sr * sin(rad)))};
	}
	// Add the last point
	if (pt_count < 362) {
		double rad = end_angle * M_PI / 180.0;
		pts[pt_count++] = (SDL_FPoint){(float)(cx + (int)(sr * cos(rad))), (float)(cy + (int)(sr * sin(rad)))};
	}

	// GPU path: the same points as 1x1 primitive rects
	if (use_gpu_rendering) {
		if (!gpu_draw_prim_is_available()) {
			return;
		}
		float nr = (float)r / 255.0f;
		float ng = (float)g / 255.0f;
		float nb = (float)b / 255.0f;
		float na = (float)alpha / 255.0f;

		for (int i = 0; i < pt_count; i++) {
			gpu_draw_rect(pts[i].x, pts[i].y, 1.0f, 1.0f, nr, ng, nb, na);
		}
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	// Single batch render call
	if (pt_count > 0) {
		SDL_RenderPoints(sdlren, pts, pt_count);
	}
}

void sdl_gradient_rect_h(int sx, int sy, int ex, int ey, unsigned short color1, unsigned short color2,
    unsigned char alpha, int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	// Apply clipping
	if (sx < clipsx) {
		sx = clipsx;
	}
	if (sy < clipsy) {
		sy = clipsy;
	}
	if (ex > clipex) {
		ex = clipex;
	}
	if (ey > clipey) {
		ey = clipey;
	}

	if (sx >= ex || sy >= ey) {
		return;
	}

	// SDL3 SDL_Vertex uses SDL_FColor with float values 0.0-1.0
	float fr1 = (float)R16TO32(color1) / 255.0f, fg1 = (float)G16TO32(color1) / 255.0f,
	      fb1 = (float)B16TO32(color1) / 255.0f;
	float fr2 = (float)R16TO32(color2) / 255.0f, fg2 = (float)G16TO32(color2) / 255.0f,
	      fb2 = (float)B16TO32(color2) / 255.0f;
	float fa = (float)alpha / 255.0f;

	// Apply offset and scale
	float fsx = (float)((sx + x_offset) * sdl_scale);
	float fsy = (float)((sy + y_offset) * sdl_scale);
	float fex = (float)((ex + x_offset) * sdl_scale);
	float fey = (float)((ey + y_offset) * sdl_scale);

	// GPU path: the same quad as two GPU triangles with per-vertex colors
	// (left side = color1, right side = color2)
	if (use_gpu_rendering) {
		if (!gpu_draw_tri_is_available()) {
			return;
		}
		float c1[4] = {fr1, fg1, fb1, fa};
		float c2[4] = {fr2, fg2, fb2, fa};

		gpu_draw_triangle(fsx, fsy, fex, fsy, fex, fey, c1, c2, c2);
		gpu_draw_triangle(fsx, fsy, fex, fey, fsx, fey, c1, c2, c1);
		return;
	}

	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	// Use SDL_RenderGeometry for GPU-accelerated gradient (single draw call)
	// Horizontal gradient: left side = color1, right side = color2
	SDL_Vertex vertices[4] = {
	    {{fsx, fsy}, {fr1, fg1, fb1, fa}, {0, 0}}, // Top-left
	    {{fex, fsy}, {fr2, fg2, fb2, fa}, {0, 0}}, // Top-right
	    {{fex, fey}, {fr2, fg2, fb2, fa}, {0, 0}}, // Bottom-right
	    {{fsx, fey}, {fr1, fg1, fb1, fa}, {0, 0}} // Bottom-left
	};

	int indices[6] = {0, 1, 2, 0, 2, 3};

	SDL_RenderGeometry(sdlren, NULL, vertices, 4, indices, 6);
}

void sdl_gradient_rect_v(int sx, int sy, int ex, int ey, unsigned short color1, unsigned short color2,
    unsigned char alpha, int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	// Apply clipping
	if (sx < clipsx) {
		sx = clipsx;
	}
	if (sy < clipsy) {
		sy = clipsy;
	}
	if (ex > clipex) {
		ex = clipex;
	}
	if (ey > clipey) {
		ey = clipey;
	}

	if (sx >= ex || sy >= ey) {
		return;
	}

	// SDL3 SDL_Vertex uses SDL_FColor with float values 0.0-1.0
	float fr1 = (float)R16TO32(color1) / 255.0f, fg1 = (float)G16TO32(color1) / 255.0f,
	      fb1 = (float)B16TO32(color1) / 255.0f;
	float fr2 = (float)R16TO32(color2) / 255.0f, fg2 = (float)G16TO32(color2) / 255.0f,
	      fb2 = (float)B16TO32(color2) / 255.0f;
	float fa = (float)alpha / 255.0f;

	// Apply offset and scale
	float fsx = (float)((sx + x_offset) * sdl_scale);
	float fsy = (float)((sy + y_offset) * sdl_scale);
	float fex = (float)((ex + x_offset) * sdl_scale);
	float fey = (float)((ey + y_offset) * sdl_scale);

	// GPU path: the same quad as two GPU triangles with per-vertex colors
	// (top = color1, bottom = color2)
	if (use_gpu_rendering) {
		if (!gpu_draw_tri_is_available()) {
			return;
		}
		float c1[4] = {fr1, fg1, fb1, fa};
		float c2[4] = {fr2, fg2, fb2, fa};

		gpu_draw_triangle(fsx, fsy, fex, fsy, fex, fey, c1, c1, c2);
		gpu_draw_triangle(fsx, fsy, fex, fey, fsx, fey, c1, c2, c2);
		return;
	}

	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	// Use SDL_RenderGeometry for GPU-accelerated gradient (single draw call)
	// Vertical gradient: top = color1, bottom = color2
	SDL_Vertex vertices[4] = {
	    {{fsx, fsy}, {fr1, fg1, fb1, fa}, {0, 0}}, // Top-left
	    {{fex, fsy}, {fr1, fg1, fb1, fa}, {0, 0}}, // Top-right
	    {{fex, fey}, {fr2, fg2, fb2, fa}, {0, 0}}, // Bottom-right
	    {{fsx, fey}, {fr2, fg2, fb2, fa}, {0, 0}} // Bottom-left
	};

	int indices[6] = {0, 1, 2, 0, 2, 3};

	SDL_RenderGeometry(sdlren, NULL, vertices, 4, indices, 6);
}

void sdl_bezier_quadratic_alpha(int x0, int y0, int x1, int y1, int x2, int y2, unsigned short color,
    unsigned char alpha, int x_offset, int y_offset)
{
	int r, g, b;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	float fx0 = (float)((x0 + x_offset) * sdl_scale);
	float fy0 = (float)((y0 + y_offset) * sdl_scale);
	float fx1 = (float)((x1 + x_offset) * sdl_scale);
	float fy1 = (float)((y1 + y_offset) * sdl_scale);
	float fx2 = (float)((x2 + x_offset) * sdl_scale);
	float fy2 = (float)((y2 + y_offset) * sdl_scale);

	// Batch render: collect all curve points, render once (33 points for 32 segments)
	SDL_FPoint pts[33];
	pts[0] = (SDL_FPoint){fx0, fy0};

	for (int i = 1; i <= 32; i++) {
		float t = (float)i / 32.0f;
		float u = 1.0f - t;
		pts[i] = (SDL_FPoint){
		    u * u * fx0 + 2.0f * u * t * fx1 + t * t * fx2, u * u * fy0 + 2.0f * u * t * fy1 + t * t * fy2};
	}

	// GPU path: the same polyline as GPU line segments
	if (use_gpu_rendering) {
		if (!gpu_draw_line_is_available()) {
			return;
		}
		float nr = (float)r / 255.0f;
		float ng = (float)g / 255.0f;
		float nb = (float)b / 255.0f;
		float na = (float)alpha / 255.0f;

		for (int i = 0; i < 32; i++) {
			gpu_draw_line(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, nr, ng, nb, na);
		}
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	SDL_RenderLines(sdlren, pts, 33);
}

void sdl_bezier_cubic_alpha(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, unsigned short color,
    unsigned char alpha, int x_offset, int y_offset)
{
	int r, g, b;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	float fx0 = (float)((x0 + x_offset) * sdl_scale);
	float fy0 = (float)((y0 + y_offset) * sdl_scale);
	float fx1 = (float)((x1 + x_offset) * sdl_scale);
	float fy1 = (float)((y1 + y_offset) * sdl_scale);
	float fx2 = (float)((x2 + x_offset) * sdl_scale);
	float fy2 = (float)((y2 + y_offset) * sdl_scale);
	float fx3 = (float)((x3 + x_offset) * sdl_scale);
	float fy3 = (float)((y3 + y_offset) * sdl_scale);

	// Batch render: collect all curve points, render once (49 points for 48 segments)
	SDL_FPoint pts[49];
	pts[0] = (SDL_FPoint){fx0, fy0};

	for (int i = 1; i <= 48; i++) {
		float t = (float)i / 48.0f;
		float u = 1.0f - t;
		float u2 = u * u;
		float u3 = u2 * u;
		float t2 = t * t;
		float t3 = t2 * t;
		pts[i] = (SDL_FPoint){u3 * fx0 + 3.0f * u2 * t * fx1 + 3.0f * u * t2 * fx2 + t3 * fx3,
		    u3 * fy0 + 3.0f * u2 * t * fy1 + 3.0f * u * t2 * fy2 + t3 * fy3};
	}

	// GPU path: the same polyline as GPU line segments
	if (use_gpu_rendering) {
		if (!gpu_draw_line_is_available()) {
			return;
		}
		float nr = (float)r / 255.0f;
		float ng = (float)g / 255.0f;
		float nb = (float)b / 255.0f;
		float na = (float)alpha / 255.0f;

		for (int i = 0; i < 48; i++) {
			gpu_draw_line(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, nr, ng, nb, na);
		}
		return;
	}

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	SDL_RenderLines(sdlren, pts, 49);
}

void sdl_gradient_circle(int cx, int cy, int radius, unsigned short color, unsigned char center_alpha,
    unsigned char edge_alpha, int x_offset, int y_offset)
{
	if (radius <= 0) {
		return;
	}

	int r = R16TO32(color);
	int g = G16TO32(color);
	int b = B16TO32(color);

	cx = (cx + x_offset) * sdl_scale;
	cy = (cy + y_offset) * sdl_scale;
	int sr = radius * sdl_scale;

	// GPU path: triangle fan with per-vertex alpha - the fan's linear
	// interpolation from center_alpha to edge_alpha is the radial gradient
	// (smoother than the concentric point circles the SDL_Renderer path
	// draws, without their moire holes)
	if (use_gpu_rendering) {
		if (!gpu_draw_tri_is_available()) {
			return;
		}
#define GRADIENT_CIRCLE_SEGMENTS 72
		float nr = (float)r / 255.0f;
		float ng = (float)g / 255.0f;
		float nb = (float)b / 255.0f;
		float cc[4] = {nr, ng, nb, (float)center_alpha / 255.0f};
		float ce[4] = {nr, ng, nb, (float)edge_alpha / 255.0f};
		float fcx = (float)cx;
		float fcy = (float)cy;
		float fsr = (float)sr;

		for (int i = 0; i < GRADIENT_CIRCLE_SEGMENTS; i++) {
			float a0 = (float)i * (2.0f * (float)M_PI / (float)GRADIENT_CIRCLE_SEGMENTS);
			float a1 = (float)(i + 1) * (2.0f * (float)M_PI / (float)GRADIENT_CIRCLE_SEGMENTS);

			gpu_draw_triangle(fcx, fcy, fcx + fsr * cosf(a0), fcy + fsr * sinf(a0), fcx + fsr * cosf(a1),
			    fcy + fsr * sinf(a1), cc, ce, ce);
		}
#undef GRADIENT_CIRCLE_SEGMENTS
		return;
	}

	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);

	// Draw concentric circles with decreasing alpha from center to edge
	for (int ri = 0; ri <= sr; ri++) {
		// Interpolate alpha based on distance from center
		float t = (float)ri / (float)sr;
		int alpha = (int)((float)center_alpha + t * ((float)edge_alpha - (float)center_alpha));
		if (alpha < 0) {
			alpha = 0;
		}
		if (alpha > 255) {
			alpha = 255;
		}

		SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);

		// Draw circle at this radius
		int x = ri, y = 0, d = 1 - ri;
		while (x >= y) {
			SDL_RenderPoint(sdlren, (float)(cx + x), (float)(cy + y));
			SDL_RenderPoint(sdlren, (float)(cx - x), (float)(cy + y));
			SDL_RenderPoint(sdlren, (float)(cx + x), (float)(cy - y));
			SDL_RenderPoint(sdlren, (float)(cx - x), (float)(cy - y));
			SDL_RenderPoint(sdlren, (float)(cx + y), (float)(cy + x));
			SDL_RenderPoint(sdlren, (float)(cx - y), (float)(cy + x));
			SDL_RenderPoint(sdlren, (float)(cx + y), (float)(cy - x));
			SDL_RenderPoint(sdlren, (float)(cx - y), (float)(cy - x));

			y++;
			if (d < 0) {
				d += 2 * y + 1;
			} else {
				x--;
				d += 2 * (y - x) + 1;
			}
		}
	}
}

/* Plot one pixel of the anti-aliased line: 1x1 GPU rect in GPU mode,
 * SDL_RenderPoint otherwise. `a` is the computed coverage alpha (0..255). */
static void aa_plot(int r, int g, int b, float a, float x, float y)
{
	if (a < 0.0f) {
		a = 0.0f;
	}
	if (a > 255.0f) {
		a = 255.0f;
	}
	if (use_gpu_rendering) {
		gpu_draw_rect(x, y, 1.0f, 1.0f, (float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, a / 255.0f);
	} else {
		SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
		SDL_RenderPoint(sdlren, x, y);
	}
}

void sdl_line_aa(int x0, int y0, int x1, int y1, unsigned short color, unsigned char alpha, int x_offset, int y_offset)
{
	// GPU path draws the same Wu pixels as 1x1 rects (via aa_plot)
	if (use_gpu_rendering && !gpu_draw_prim_is_available()) {
		return;
	}

	// Xiaolin Wu's line algorithm for anti-aliased lines
	int r = R16TO32(color);
	int g = G16TO32(color);
	int b = B16TO32(color);

	x0 = (x0 + x_offset) * sdl_scale;
	y0 = (y0 + y_offset) * sdl_scale;
	x1 = (x1 + x_offset) * sdl_scale;
	y1 = (y1 + y_offset) * sdl_scale;

	if (!use_gpu_rendering) {
		SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);
	}

	int steep = abs(y1 - y0) > abs(x1 - x0);
	if (steep) {
		int tmp = x0;
		x0 = y0;
		y0 = tmp;
		tmp = x1;
		x1 = y1;
		y1 = tmp;
	}
	if (x0 > x1) {
		int tmp = x0;
		x0 = x1;
		x1 = tmp;
		tmp = y0;
		y0 = y1;
		y1 = tmp;
	}

	float dx = (float)(x1 - x0);
	float dy = (float)(y1 - y0);
	float gradient = (dx < 0.001f) ? 1.0f : dy / dx;

	// Handle first endpoint
	float xend = (float)x0;
	float yend = (float)y0 + gradient * (xend - (float)x0);
	float xgap = 1.0f - ((float)x0 + 0.5f - floorf((float)x0 + 0.5f));
	int xpxl1 = (int)xend;
	int ypxl1 = (int)floorf(yend);

	if (steep) {
		aa_plot(r, g, b, (float)alpha * (1.0f - (yend - floorf(yend))) * xgap, (float)ypxl1, (float)xpxl1);
		aa_plot(r, g, b, (float)alpha * (yend - floorf(yend)) * xgap, (float)(ypxl1 + 1), (float)xpxl1);
	} else {
		aa_plot(r, g, b, (float)alpha * (1.0f - (yend - floorf(yend))) * xgap, (float)xpxl1, (float)ypxl1);
		aa_plot(r, g, b, (float)alpha * (yend - floorf(yend)) * xgap, (float)xpxl1, (float)(ypxl1 + 1));
	}

	float intery = yend + gradient;

	// Handle second endpoint
	xend = (float)x1;
	yend = (float)y1 + gradient * (xend - (float)x1);
	xgap = (float)x1 + 0.5f - floorf((float)x1 + 0.5f);
	int xpxl2 = (int)xend;
	int ypxl2 = (int)floorf(yend);

	if (steep) {
		aa_plot(r, g, b, (float)alpha * (1.0f - (yend - floorf(yend))) * xgap, (float)ypxl2, (float)xpxl2);
		aa_plot(r, g, b, (float)alpha * (yend - floorf(yend)) * xgap, (float)(ypxl2 + 1), (float)xpxl2);
	} else {
		aa_plot(r, g, b, (float)alpha * (1.0f - (yend - floorf(yend))) * xgap, (float)xpxl2, (float)ypxl2);
		aa_plot(r, g, b, (float)alpha * (yend - floorf(yend)) * xgap, (float)xpxl2, (float)(ypxl2 + 1));
	}

	// Main loop
	for (int x = xpxl1 + 1; x < xpxl2; x++) {
		if (steep) {
			aa_plot(r, g, b, (float)alpha * (1.0f - (intery - floorf(intery))), floorf(intery), (float)x);
			aa_plot(r, g, b, (float)alpha * (intery - floorf(intery)), floorf(intery) + 1, (float)x);
		} else {
			aa_plot(r, g, b, (float)alpha * (1.0f - (intery - floorf(intery))), (float)x, floorf(intery));
			aa_plot(r, g, b, (float)alpha * (intery - floorf(intery)), (float)x, floorf(intery) + 1);
		}
		intery += gradient;
	}
}

void sdl_ring_alpha(int cx, int cy, int inner_radius, int outer_radius, int start_angle, int end_angle,
    unsigned short color, unsigned char alpha, int x_offset, int y_offset)
{
	int r, g, b;
	int gpu_lines = 0;
	float nr = 0.0f, ng = 0.0f, nb = 0.0f, na = 0.0f;

	if (inner_radius <= 0 || outer_radius <= 0 || outer_radius <= inner_radius) {
		return;
	}

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	// GPU path draws the same radial lines via the GPU line pipeline
	if (use_gpu_rendering) {
		if (!gpu_draw_line_is_available()) {
			return;
		}
		gpu_lines = 1;
		nr = (float)r / 255.0f;
		ng = (float)g / 255.0f;
		nb = (float)b / 255.0f;
		na = (float)alpha / 255.0f;
	} else {
		SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
		SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);
	}

	cx = (cx + x_offset) * sdl_scale;
	cy = (cy + y_offset) * sdl_scale;
	inner_radius *= sdl_scale;
	outer_radius *= sdl_scale;

	// Normalize angles
	while (start_angle < 0) {
		start_angle += 360;
	}
	while (end_angle < 0) {
		end_angle += 360;
	}
	start_angle %= 360;
	end_angle %= 360;

	// Draw filled ring segment
	for (int angle = start_angle; angle != end_angle; angle = (angle + 1) % 360) {
		double rad = (double)angle * M_PI / 180.0;
		double cos_a = cos(rad);
		double sin_a = sin(rad);

		// Draw radial line from inner to outer radius
		int x1 = cx + (int)((double)inner_radius * cos_a);
		int y1 = cy + (int)((double)inner_radius * sin_a);
		int x2 = cx + (int)((double)outer_radius * cos_a);
		int y2 = cy + (int)((double)outer_radius * sin_a);
		if (gpu_lines) {
			gpu_draw_line((float)x1, (float)y1, (float)x2, (float)y2, nr, ng, nb, na);
		} else {
			SDL_RenderLine(sdlren, (float)x1, (float)y1, (float)x2, (float)y2);
		}
	}
	// Draw the last segment
	double rad = (double)end_angle * M_PI / 180.0;
	double cos_a = cos(rad);
	double sin_a = sin(rad);
	int x1 = cx + (int)((double)inner_radius * cos_a);
	int y1 = cy + (int)((double)inner_radius * sin_a);
	int x2 = cx + (int)((double)outer_radius * cos_a);
	int y2 = cy + (int)((double)outer_radius * sin_a);
	if (gpu_lines) {
		gpu_draw_line((float)x1, (float)y1, (float)x2, (float)y2, nr, ng, nb, na);
	} else {
		SDL_RenderLine(sdlren, (float)x1, (float)y1, (float)x2, (float)y2);
	}
}
