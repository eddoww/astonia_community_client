/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL - Drawing Module
 *
 * Drawing functions: blit, text rendering, rectangles, lines, pixels, circles, bargraphs.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "dll.h"
#include "astonia.h"
#include "sdl/sdl.h"
#include "sdl/sdl_private.h"

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

static void sdl_blit_tex(
    SDL_Texture *tex, int sx, int sy, int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	int addx = 0, addy = 0;
	float f_dx, f_dy;
	SDL_FRect dr, sr;
	Uint64 start = SDL_GetTicks();

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
	if (sdlt[cache_index].tex) {
		sdl_blit_tex(sdlt[cache_index].tex, sx, sy, clipsx, clipsy, clipex, clipey, x_offset, y_offset);
	}
}

SDL_Texture *sdl_maketext(const char *text, struct renderfont *font, uint32_t color, int flags)
{
	uint32_t *pixel, *dst;
	unsigned char *rawrun;
	int x, y = 0, sizex, sizey = 0, sx = 0;
	const char *c, *otext = text;
	Uint64 start = SDL_GetTicks();

	for (sizex = 0, c = text; *c; c++) {
		sizex += font[(unsigned char)*c].dim * sdl_scale;
	}

	if (flags & (RENDER__FRAMED_FONT | RENDER__SHADED_FONT)) {
		sizex += sdl_scale * 2;
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

	if (sizex < 1) {
		sizex = 1;
	}
	if (sizey < 1) {
		sizey = 1;
	}

	sizey++;
	sdl_time_text += (long long)(SDL_GetTicks() - start);

	start = SDL_GetTicks();
	SDL_Texture *texture = SDL_CreateTexture(sdlren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, sizex, sizey);
	if (texture) {
		SDL_UpdateTexture(texture, NULL, pixel, (int)((size_t)sizex * sizeof(uint32_t)));
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	} else {
		warn("SDL_texture Error: %s maketext (%s)", SDL_GetError(), otext);
	}
#ifdef SDL_FAST_MALLOC
	FREE(pixel);
#else
	xfree(pixel);
#endif
	sdl_time_tex += SDL_GetTicks() - start;

	return texture;
}

int sdl_drawtext(int sx, int sy, unsigned short int color, int flags, const char *text, struct renderfont *font,
    int clipsx, int clipsy, int clipex, int clipey, int x_offset, int y_offset)
{
	int dx, cache_index;
	SDL_Texture *tex;
	int r, g, b, a;
	const char *c;

	if (!*text) {
		return sx;
	}

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);
	a = 255;

	if (flags & RENDER_TEXT_NOCACHE) {
		tex = sdl_maketext(text, font, (uint32_t)IRGBA(r, g, b, a), flags);
	} else {
		cache_index = sdl_tx_load(
		    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, text, (int)IRGBA(r, g, b, a), flags, font, 0, 0);
		tex = sdlt[cache_index].tex;
	}

	for (dx = 0, c = text; *c; c++) {
		dx += font[(unsigned char)*c].dim;
	}

	if (tex) {
		if (flags & RENDER_ALIGN_CENTER) {
			sx -= dx / 2;
		} else if (flags & RENDER_TEXT_RIGHT) {
			sx -= dx;
		}

		sdl_blit_tex(tex, sx, sy, clipsx, clipsy, clipex, clipey, x_offset, y_offset);

		if (flags & RENDER_TEXT_NOCACHE) {
			SDL_DestroyTexture(tex);
		}
	}

	return sx + dx;
}

void sdl_rect(int sx, int sy, int ex, int ey, unsigned short int color, int clipsx, int clipsy, int clipex, int clipey,
    int x_offset, int y_offset)
{
	int r, g, b, a;
	SDL_FRect rc;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);
	a = 255;

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
	SDL_FRect rc;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);
	a = alpha;

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

	rc.x = (float)((sx + x_offset) * sdl_scale);
	rc.w = (float)((ex - sx) * sdl_scale);
	rc.y = (float)((sy + y_offset) * sdl_scale);
	rc.h = (float)((ey - sy) * sdl_scale);

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
	SDL_SetRenderDrawBlendMode(sdlren, SDL_BLENDMODE_BLEND);
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

void sdl_line(int fx, int fy, int tx, int ty, unsigned short color, int clipsx, int clipsy, int clipex, int clipey,
    int x_offset, int y_offset)
{
	int r, g, b, a;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);
	a = 255;

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
	SDL_Point pt[16];

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, SDL_BLENDMODE_BLEND);
	switch (sdl_scale) {
	case 1:
		SDL_RenderDrawPoint(sdlren, x + x_offset, y + y_offset);
		return;
	case 2:
		pt[0].x = (x + x_offset) * sdl_scale;
		pt[0].y = (y + y_offset) * sdl_scale;
		pt[1].x = pt[0].x + 1;
		pt[1].y = pt[0].y;
		pt[2].x = pt[0].x;
		pt[2].y = pt[0].y + 1;
		pt[3].x = pt[0].x + 1;
		pt[3].y = pt[0].y + 1;
		i = 4;
		break;
	case 3:
		pt[0].x = (x + x_offset) * sdl_scale;
		pt[0].y = (y + y_offset) * sdl_scale;
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
		pt[0].x = (x + x_offset) * sdl_scale;
		pt[0].y = (y + y_offset) * sdl_scale;
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
	SDL_RenderDrawPoints(sdlren, pt, i);
}

void sdl_line_alpha(int fx, int fy, int tx, int ty, unsigned short color, unsigned char alpha, int clipsx, int clipsy,
    int clipex, int clipey, int x_offset, int y_offset)
{
	int r, g, b;

	r = R16TO32(color);
	g = G16TO32(color);
	b = B16TO32(color);

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

	SDL_SetRenderDrawColor(sdlren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)alpha);
	SDL_SetRenderDrawBlendMode(sdlren, SDL_BLENDMODE_BLEND);
	SDL_RenderDrawLine(sdlren, fx * sdl_scale, fy * sdl_scale, tx * sdl_scale, ty * sdl_scale);
}

// Current blend mode for rendering operations
static SDL_BlendMode current_blend_mode = SDL_BLENDMODE_BLEND;

void sdl_set_blend_mode(int mode)
{
	switch (mode) {
	case 0: // BLEND_NORMAL
		current_blend_mode = SDL_BLENDMODE_BLEND;
		break;
	case 1: // BLEND_ADDITIVE
		current_blend_mode = SDL_BLENDMODE_ADD;
		break;
	case 2: // BLEND_MULTIPLY
		current_blend_mode = SDL_BLENDMODE_MOD;
		break;
	case 3: // BLEND_SCREEN (simulated with additive for SDL2)
		current_blend_mode = SDL_BLENDMODE_ADD;
		break;
	default:
		current_blend_mode = SDL_BLENDMODE_BLEND;
		break;
	}
	SDL_SetRenderDrawBlendMode(sdlren, current_blend_mode);
}

int sdl_get_blend_mode(void)
{
	switch (current_blend_mode) {
	case SDL_BLENDMODE_BLEND:
		return 0;
	case SDL_BLENDMODE_ADD:
		return 1;
	case SDL_BLENDMODE_MOD:
		return 2;
	default:
		return 0;
	}
}

// ============================================================================
// Custom Texture Loading API for Modders
// ============================================================================

#define MAX_MOD_TEXTURES 256

struct mod_texture {
	SDL_Texture *tex;
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

/**
 * Validate a path for safety before loading.
 * Rejects path traversal attempts and absolute paths.
 *
 * @param path The path to validate
 * @return 1 if safe, 0 if rejected
 */
static int validate_mod_path(const char *path)
{
	const char *p;

	if (!path || !*path) {
		return 0;
	}

	// Reject absolute paths (Unix or Windows style)
	if (path[0] == '/' || path[0] == '\\') {
		warn("mod texture: absolute paths not allowed: %s", path);
		return 0;
	}
	if (strlen(path) >= 2 && path[1] == ':') {
		warn("mod texture: absolute paths not allowed: %s", path);
		return 0;
	}

	// Reject path traversal sequences
	p = path;
	while (*p) {
		// Check for ".." at start or after path separator
		if (p[0] == '.' && p[1] == '.') {
			if (p == path || p[-1] == '/' || p[-1] == '\\') {
				if (p[2] == '\0' || p[2] == '/' || p[2] == '\\') {
					warn("mod texture: path traversal not allowed: %s", path);
					return 0;
				}
			}
		}
		// Reject null bytes
		if (*p == '\0') {
			break;
		}
		p++;
	}

	return 1;
}

int sdl_load_mod_texture(const char *path)
{
	int dx, dy;
	uint32_t *pixel;
	SDL_Texture *tex;
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

#ifdef SDL_FAST_MALLOC
	FREE(pixel);
#else
	xfree(pixel);
#endif

	mod_textures[i].tex = tex;
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
	mod_textures[tex_id].tex = NULL;
	mod_textures[tex_id].width = 0;
	mod_textures[tex_id].height = 0;
	mod_textures[tex_id].used = 0;
}

void sdl_render_mod_texture(int tex_id, int x, int y, unsigned char alpha, int clipsx, int clipsy, int clipex,
    int clipey, int x_offset, int y_offset)
{
	SDL_Rect dr, sr;
	int dx, dy, addx = 0, addy = 0;

	if (tex_id < 0 || tex_id >= MAX_MOD_TEXTURES) {
		return;
	}
	if (!mod_textures[tex_id].used || !mod_textures[tex_id].tex) {
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

	sr.x = addx;
	sr.y = addy;
	sr.w = dx;
	sr.h = dy;

	dr.x = (x + x_offset) * sdl_scale;
	dr.y = (y + y_offset) * sdl_scale;
	dr.w = dx * sdl_scale;
	dr.h = dy * sdl_scale;

	SDL_SetTextureAlphaMod(mod_textures[tex_id].tex, alpha);
	SDL_RenderCopy(sdlren, mod_textures[tex_id].tex, &sr, &dr);
}

void sdl_render_mod_texture_scaled(int tex_id, int x, int y, float scale, unsigned char alpha, int clipsx, int clipsy,
    int clipex, int clipey, int x_offset, int y_offset)
{
	SDL_Rect dr, sr;
	int dx, dy, scaled_dx, scaled_dy;

	if (tex_id < 0 || tex_id >= MAX_MOD_TEXTURES) {
		return;
	}
	if (!mod_textures[tex_id].used || !mod_textures[tex_id].tex) {
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

	sr.x = 0;
	sr.y = 0;
	sr.w = dx;
	sr.h = dy;

	dr.x = (x + x_offset) * sdl_scale;
	dr.y = (y + y_offset) * sdl_scale;
	dr.w = scaled_dx * sdl_scale;
	dr.h = scaled_dy * sdl_scale;

	SDL_SetTextureAlphaMod(mod_textures[tex_id].tex, alpha);
	SDL_RenderCopy(sdlren, mod_textures[tex_id].tex, &sr, &dr);
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
	SDL_Texture *tex;
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
	SDL_Texture *tex;
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

	// Create render target texture
	tex = SDL_CreateTexture(
	    sdlren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width * sdl_scale, height * sdl_scale);
	if (!tex) {
		warn("failed to create render target: %s", SDL_GetError());
		return -1;
	}

	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

	render_targets[i].tex = tex;
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
		SDL_SetRenderTarget(sdlren, NULL);
		current_render_target = -1;
	}

	if (render_targets[target_id].tex) {
		SDL_DestroyTexture(render_targets[target_id].tex);
	}
	render_targets[target_id].tex = NULL;
	render_targets[target_id].width = 0;
	render_targets[target_id].height = 0;
	render_targets[target_id].used = 0;
}

int sdl_set_render_target(int target_id)
{
	if (target_id < 0) {
		// Reset to screen
		SDL_SetRenderTarget(sdlren, NULL);
		current_render_target = -1;
		return 0;
	}

	if (target_id >= MAX_RENDER_TARGETS || !render_targets[target_id].used) {
		return -1;
	}

	SDL_SetRenderTarget(sdlren, render_targets[target_id].tex);
	current_render_target = target_id;
	return 0;
}

void sdl_render_target_to_screen(int target_id, int x, int y, unsigned char alpha)
{
	SDL_Rect dr;

	if (target_id < 0 || target_id >= MAX_RENDER_TARGETS) {
		return;
	}
	if (!render_targets[target_id].used || !render_targets[target_id].tex) {
		return;
	}

	// Make sure we're rendering to the screen
	if (current_render_target >= 0) {
		SDL_SetRenderTarget(sdlren, NULL);
	}

	dr.x = x * sdl_scale;
	dr.y = y * sdl_scale;
	dr.w = render_targets[target_id].width * sdl_scale;
	dr.h = render_targets[target_id].height * sdl_scale;

	SDL_SetTextureAlphaMod(render_targets[target_id].tex, alpha);
	SDL_RenderCopy(sdlren, render_targets[target_id].tex, NULL, &dr);

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
	if (!render_targets[target_id].used || !render_targets[target_id].tex) {
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

	SDL_SetRenderDrawColor(
	    sdlren, (Uint8)IGET_R(color), (Uint8)IGET_G(color), (Uint8)IGET_B(color), (Uint8)IGET_A(color));
	SDL_RenderPoints(sdlren, pts, (int)dC);
}
