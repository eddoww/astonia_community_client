/*
 * Text-rendering correctness harness (GPU redesign phase 2).
 *
 * The batched text path draws per-glyph instanced quads from a glyph
 * atlas instead of per-string cached textures. This harness proves the
 * output is identical to today's path BEFORE the client integration:
 *
 * Part A (CPU, always runs, bit-exact):
 *   For every printable character of every bitmap font variant (fonta/b/c
 *   + derived _shaded/_framed, sdl_scale 1 and 2), composite the per-glyph
 *   masks (sdl_text_glyph.c) at the string advances and memcmp against the
 *   REAL string rasterizer sdl_rendertext_to_pixels() (sdl_draw.c).
 *
 * Part B (GPU, SKIPs without a device, tolerance 0 for bitmap masks):
 *   Renders composited scenes (background + text) through BOTH real
 *   pipelines - the parity sprite_simple draw of a whole-string texture
 *   vs per-glyph GPU_FX_MODE_PLAIN instances through sprite_fx - and
 *   diffs the readback. Also covers the single-quad "tier 2" path
 *   (pre-composed string pixels in an atlas page) incl. alpha modulation.
 *
 * Part C (TTF, needs sdl3-ttf at build time - compiled under
 *   HAVE_TTF_COMPARE - and res/fonts/, else SKIPs, bit-exact):
 *   Proves the FreeType-I/O fix is invisible: rendering with a dedicated
 *   outline font handle opened from a MEMORY buffer equals the legacy
 *   single-handle TTF_SetFontOutline() toggle (which flushes SDL_ttf's
 *   glyph cache every call - the profiled per-frame glyph file I/O).
 *
 * Run from the repo root (shaders from res/shaders/compiled/, fonts from
 * res/). Exit 0 on pass/skip, 1 on any failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <SDL3/SDL.h>

#include "astonia.h"
#include "game/memory.h"
#include "sdl/sdl.h"
#include "sdl/sdl_private.h"
#include "sdl/sdl_text_glyph.h"
#include "sdl/sdl_gpu_shaderfx.h"
#include "sdl/sdl_gpu.h"

/* bitmap fonts (embedded data, src/game/font.c) */
extern RenderFont fonta[], fontb[], fontc[];

/* text flag bits (fixed API values, see game.h / sdl_draw.c) */
#define TF_SHADED_FONT 128
#define TF_FRAMED_FONT 256

static int failures;

/* ========================================================================
 * Derived font variants - copied from src/game/render.c (static there).
 * Provenance: render_create_letter / render_create_rawrun /
 * create_shade_font / create_frame_font / render_create_font_png must
 * match render.c exactly; they generate the same rawrun data the game
 * uses at runtime.
 * ======================================================================== */

static RenderFont fonta_shaded[128], fonta_framed[128];
static RenderFont fontb_shaded[128], fontb_framed[128];
static RenderFont fontc_shaded[128], fontc_framed[128];

static void render_create_letter(unsigned char *rawrun, int sx, int sy, int val, char letter[64][64])
{
	int x = sx, y = sy;

	while (*rawrun != 255) {
		if (*rawrun == 254) {
			y++;
			x = sx;
			rawrun++;
			continue;
		}
		x += *rawrun++;
		letter[y][x] = (char)val;
	}
}

static unsigned char *render_create_rawrun(char letter[64][64])
{
	char *ptr, *fon, *last;
	int x, y, step;

	last = fon = ptr = xmalloc(8192, MEM_TEMP);

	for (y = sdl_scale * 3; y < 64; y++) {
		step = 0;
		for (x = sdl_scale * 3; x < 64; x++) {
			if (letter[y][x] == 2) {
				*ptr++ = (char)step;
				last = ptr;
				step = 1;
			} else {
				step++;
			}
		}
		*ptr++ = (char)254;
	}
	ptr = last;
	*ptr++ = (char)255;

	fon = xrealloc(fon, (size_t)(ptr - fon), MEM_GLOB);
	return (unsigned char *)fon;
}

static void create_shade_font(RenderFont *src, RenderFont *dst)
{
	char letter[64][64];
	int c, x, y;

	for (c = 0; c < 128; c++) {
		memset(letter, 0, sizeof(letter));
		for (y = 0; y <= sdl_scale; y++) {
			for (x = 0; x <= sdl_scale; x++) {
				if (x > 0 || y > 0) {
					render_create_letter(src[c].raw, sdl_scale * 4 + x, sdl_scale * 4 + y, 2, letter);
				}
			}
		}
		render_create_letter(src[c].raw, sdl_scale * 4, sdl_scale * 4, 1, letter);
		dst[c].raw = render_create_rawrun(letter);
		dst[c].dim = src[c].dim;
	}
}

static void create_frame_font(RenderFont *src, RenderFont *dst)
{
	char letter[64][64];
	int c, x, y;

	for (c = 0; c < 128; c++) {
		memset(letter, 0, sizeof(letter));
		for (y = 0; y <= sdl_scale * 2; y += sdl_scale) {
			for (x = 0; x <= sdl_scale * 2; x += sdl_scale) {
				render_create_letter(src[c].raw, sdl_scale * 3 + x, sdl_scale * 3 + y, 2, letter);
			}
		}
		render_create_letter(src[c].raw, sdl_scale * 4, sdl_scale * 4, 1, letter);
		dst[c].raw = render_create_rawrun(letter);
		dst[c].dim = src[c].dim;
	}
}

static int test_create_font_png(RenderFont *dst, uint32_t *pixel, int dx, int yoff, int scale)
{
	int c, x, y, sx, sy;
	char letter[64][64];

	for (c = 32; c < 128; c++) {
		if (c < 80) {
			sx = (c - 32) * 10 * scale;
			sy = yoff;
		} else {
			sx = (c - 80) * 10 * scale;
			sy = yoff + 20 * scale;
		}
		memset(letter, 0, sizeof(letter));

		for (x = 0; x < 10 * scale; x++) {
			for (y = 0; y < 12 * scale; y++) {
				if (pixel[sx + x + (sy + y) * dx] == 0xffffffff) {
					letter[y + sdl_scale * 3][x + sdl_scale * 3] = 2;
				}
			}
		}
		dst[c].raw = render_create_rawrun(letter);
	}
	return 1;
}

/* Build the base fonts for the CURRENT sdl_scale (scale > 1 replaces the
 * embedded rasters from res/fontNx.png like render_create_font does)
 * and derive the shaded/framed variants. Returns 0 when the PNG for the
 * scale is missing. */
static int build_fonts_for_scale(void)
{
	if (sdl_scale > 1) {
		char path[64];
		int dx, dy;
		uint32_t *pixel;

		snprintf(path, sizeof(path), "res/font%dx.png", sdl_scale);
		pixel = sdl_load_png(path, &dx, &dy);
		if (!pixel) {
			return 0;
		}
		test_create_font_png(fonta, pixel, dx, 40 * sdl_scale, sdl_scale);
		test_create_font_png(fontb, pixel, dx, 0, sdl_scale);
		test_create_font_png(fontc, pixel, dx, 80 * sdl_scale, sdl_scale);
		FREE(pixel);
	}

	create_shade_font(fonta, fonta_shaded);
	create_shade_font(fontb, fontb_shaded);
	create_shade_font(fontc, fontc_shaded);
	create_frame_font(fonta, fonta_framed);
	create_frame_font(fontb, fontb_framed);
	create_frame_font(fontc, fontc_framed);
	return 1;
}

/* ========================================================================
 * Part A: glyph-composite vs string-rasterizer, bit-exact
 * ======================================================================== */

/* Composite per-glyph masks at the string advances into a sizex x sizey
 * buffer, replicating what the batched GPU path draws. Returns 0 when a
 * glyph inks outside the reference buffer (which would falsify per-glyph
 * batching - must never happen). */
static int compose_glyph_string(
    const char *text, RenderFont *font, uint32_t color, int sizex, int sizey, uint32_t *out)
{
	uint32_t buffer[TEXT_GLYPH_MAX_DIM * TEXT_GLYPH_MAX_DIM];
	text_glyph_mask_t m;
	int pen = 0;
	const char *c;

	memset(out, 0, (size_t)sizex * sizey * sizeof(uint32_t));

	for (c = text; *c && *c != RENDER_TEXT_TERMINATOR; c++) {
		if (*c < 0) {
			continue; /* string rasterizer PANIC-skips these without advance */
		}
		if (!text_glyph_rasterize(font, (unsigned char)*c, sdl_scale, buffer, &m)) {
			return 0;
		}
		for (int y = 0; y < m.h; y++) {
			for (int x = 0; x < m.w; x++) {
				if (!m.pixel[x + y * m.w]) {
					continue;
				}
				if (pen + x >= sizex || y >= sizey) {
					fprintf(stderr, "    glyph '%c' inks outside the string buffer (%d,%d vs %dx%d)\n", *c, pen + x, y,
					    sizex, sizey);
					return 0;
				}
				out[pen + x + y * sizex] = color;
			}
		}
		pen += m.advance;
	}
	return 1;
}

static void part_a_case(const char *name, const char *text, RenderFont *font, uint32_t color, int flags)
{
	int sizex = 0, sizey = 0;
	uint32_t *ref = sdl_rendertext_to_pixels(text, font, color, flags, &sizex, &sizey);
	uint32_t *got;

	if (!ref) {
		printf("A %-34s FAIL (reference rasterizer returned NULL)\n", name);
		failures++;
		return;
	}

	got = malloc((size_t)sizex * sizey * sizeof(uint32_t));
	if (!compose_glyph_string(text, font, color, sizex, sizey, got)) {
		printf("A %-34s FAIL (glyph composite impossible)\n", name);
		failures++;
	} else if (memcmp(ref, got, (size_t)sizex * sizey * sizeof(uint32_t)) != 0) {
		int bad = 0, fx = -1, fy = -1;
		for (int i = 0; i < sizex * sizey; i++) {
			if (ref[i] != got[i]) {
				if (!bad) {
					fx = i % sizex;
					fy = i / sizex;
				}
				bad++;
			}
		}
		printf("A %-34s FAIL (%d px differ, first at %d,%d: ref %08x got %08x)\n", name, bad, fx, fy,
		    ref[fx + fy * sizex], got[fx + fy * sizex]);
		failures++;
	} else {
		printf("A %-34s PASS (bit-exact, %dx%d)\n", name, sizex, sizey);
	}

	free(got);
	FREE(ref);
}

static void run_part_a(void)
{
	/* every printable ASCII character, plus a terminator tail that must
	 * not render */
	char all[128];
	int n = 0;
	for (int c = 32; c < 127; c++) {
		all[n++] = (char)c;
	}
	all[n++] = RENDER_TEXT_TERMINATOR;
	all[n++] = 'X'; /* must not appear */
	all[n] = 0;

	struct {
		const char *name;
		RenderFont *font;
		int flags;
	} variants[] = {
	    {"fonta", fonta, 0},
	    {"fontb", fontb, 0},
	    {"fontc", fontc, 0},
	    {"fonta_shaded", fonta_shaded, TF_SHADED_FONT},
	    {"fontb_shaded", fontb_shaded, TF_SHADED_FONT},
	    {"fontc_shaded", fontc_shaded, TF_SHADED_FONT},
	    {"fonta_framed", fonta_framed, TF_FRAMED_FONT},
	    {"fontb_framed", fontb_framed, TF_FRAMED_FONT},
	    {"fontc_framed", fontc_framed, TF_FRAMED_FONT},
	};

	for (size_t v = 0; v < sizeof(variants) / sizeof(variants[0]); v++) {
		char name[64];
		snprintf(name, sizeof(name), "%s-s%d-ascii", variants[v].name, sdl_scale);
		part_a_case(name, all, variants[v].font, 0xFFFFFFFFu, variants[v].flags);
		snprintf(name, sizeof(name), "%s-s%d-color", variants[v].name, sdl_scale);
		part_a_case(name, "Hello, Cameron! 123 [HP 45/99]", variants[v].font, 0xFF44CC88u, variants[v].flags);
	}
}

/* ========================================================================
 * Part B: composited scenes through the real GPU pipelines
 * ======================================================================== */

static SDL_GPUDevice *dev;
static SDL_GPUGraphicsPipeline *parity_pipeline; /* sprite_simple, blend 0 */
static SDL_GPUGraphicsPipeline *fx_pipeline; /* sprite_fx, blend on */
static SDL_GPUBuffer *quad_vbo;
static SDL_GPUBuffer *inst_buf;
static SDL_GPUSampler *sampler;

#define TARGET_W 512
#define TARGET_H 128
#define MAX_INST 1024
#define PAGE_SIZE 1024

/* std140 uniform of sprite_simple.vert (mirrors sprite_push_constants_t
 * in sdl_gpu_draw.c - fixed shader ABI) */
typedef struct parity_uniforms {
	float dest_x, dest_y, dest_w, dest_h;
	float src_u, src_v, src_w, src_h;
	float color_r, color_g, color_b, color_a;
	float screen_w, screen_h;
	float pad0, pad1;
} parity_uniforms_t;

typedef struct quad_vertex {
	float x, y, u, v;
} quad_vertex_t;

static const quad_vertex_t quad_vertices[6] = {
    {0, 0, 0, 0},
    {1, 0, 1, 0},
    {1, 1, 1, 1},
    {0, 0, 0, 0},
    {1, 1, 1, 1},
    {0, 1, 0, 1},
};

static SDL_GPUShader *load_shader_file(
    const char *path, SDL_GPUShaderStage stage, Uint32 num_samplers, Uint32 num_storage, Uint32 num_uniform)
{
	size_t size;
	void *code = SDL_LoadFile(path, &size);
	if (!code) {
		fprintf(stderr, "cannot load %s: %s\n", path, SDL_GetError());
		return NULL;
	}
	SDL_GPUShaderCreateInfo info = {
	    .code = code,
	    .code_size = size,
	    .entrypoint = "main",
	    .format = SDL_GPU_SHADERFORMAT_SPIRV,
	    .stage = stage,
	    .num_samplers = num_samplers,
	    .num_storage_textures = 0,
	    .num_storage_buffers = num_storage,
	    .num_uniform_buffers = num_uniform,
	};
	SDL_GPUShader *sh = SDL_CreateGPUShader(dev, &info);
	SDL_free(code);
	if (!sh) {
		fprintf(stderr, "shader create failed for %s: %s\n", path, SDL_GetError());
	}
	return sh;
}

static int upload_buffer(SDL_GPUBuffer *buf, const void *data, size_t size)
{
	SDL_GPUTransferBufferCreateInfo ti = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = (Uint32)size};
	SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &ti);
	if (!tb) {
		return -1;
	}
	void *m = SDL_MapGPUTransferBuffer(dev, tb, false);
	if (!m) {
		SDL_ReleaseGPUTransferBuffer(dev, tb);
		return -1;
	}
	memcpy(m, data, size);
	SDL_UnmapGPUTransferBuffer(dev, tb);

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTransferBufferLocation src = {.transfer_buffer = tb, .offset = 0};
	SDL_GPUBufferRegion dst = {.buffer = buf, .offset = 0, .size = (Uint32)size};
	SDL_UploadToGPUBuffer(cp, &src, &dst, false);
	SDL_EndGPUCopyPass(cp);
	SDL_SubmitGPUCommandBuffer(cmd);
	SDL_ReleaseGPUTransferBuffer(dev, tb);
	return 0;
}

static SDL_GPUTexture *create_texture(int w, int h, int target)
{
	SDL_GPUTextureCreateInfo tci = {
	    .type = SDL_GPU_TEXTURETYPE_2D,
	    .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
	    .usage = target ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : SDL_GPU_TEXTUREUSAGE_SAMPLER,
	    .width = (Uint32)w,
	    .height = (Uint32)h,
	    .layer_count_or_depth = 1,
	    .num_levels = 1,
	    .sample_count = SDL_GPU_SAMPLECOUNT_1,
	};
	return SDL_CreateGPUTexture(dev, &tci);
}

static int upload_texture_region(SDL_GPUTexture *tex, const uint32_t *pixels, int x, int y, int w, int h)
{
	size_t size = (size_t)w * h * sizeof(uint32_t);
	SDL_GPUTransferBufferCreateInfo ti = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = (Uint32)size};
	SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &ti);
	if (!tb) {
		return -1;
	}
	void *m = SDL_MapGPUTransferBuffer(dev, tb, false);
	if (!m) {
		SDL_ReleaseGPUTransferBuffer(dev, tb);
		return -1;
	}
	memcpy(m, pixels, size);
	SDL_UnmapGPUTransferBuffer(dev, tb);

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTextureTransferInfo tti = {
	    .transfer_buffer = tb, .offset = 0, .pixels_per_row = (Uint32)w, .rows_per_layer = (Uint32)h};
	SDL_GPUTextureRegion reg = {.texture = tex, .x = (Uint32)x, .y = (Uint32)y, .w = (Uint32)w, .h = (Uint32)h, .d = 1};
	SDL_UploadToGPUTexture(cp, &tti, &reg, false);
	SDL_EndGPUCopyPass(cp);
	SDL_SubmitGPUCommandBuffer(cmd);
	SDL_ReleaseGPUTransferBuffer(dev, tb);
	return 0;
}

static int gpu_setup(void)
{
	dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, NULL);
	if (!dev) {
		return -1;
	}

	/* parity pipeline: sprite_simple + standard BLEND (mode 0) */
	{
		SDL_GPUShader *vs =
		    load_shader_file("res/shaders/compiled/sprite_simple_vs.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 1);
		SDL_GPUShader *ps =
		    load_shader_file("res/shaders/compiled/sprite_simple_ps.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0, 0);
		if (!vs || !ps) {
			return -1;
		}
		SDL_GPUVertexBufferDescription vb_desc = {
		    .slot = 0, .pitch = sizeof(quad_vertex_t), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX};
		SDL_GPUVertexAttribute attrs[2] = {
		    {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0},
		    {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 8},
		};
		SDL_GPUColorTargetDescription color_desc = {
		    .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
		    .blend_state = {.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
		        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
		        .color_blend_op = SDL_GPU_BLENDOP_ADD,
		        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
		        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
		        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
		        .enable_blend = true},
		};
		SDL_GPUGraphicsPipelineCreateInfo pi = {
		    .vertex_shader = vs,
		    .fragment_shader = ps,
		    .vertex_input_state = {.vertex_buffer_descriptions = &vb_desc,
		        .num_vertex_buffers = 1,
		        .vertex_attributes = attrs,
		        .num_vertex_attributes = 2},
		    .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		    .rasterizer_state = {.fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE},
		    .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1, .sample_mask = 0xFFFFFFFF},
		    .target_info = {.color_target_descriptions = &color_desc, .num_color_targets = 1},
		};
		parity_pipeline = SDL_CreateGPUGraphicsPipeline(dev, &pi);
		SDL_ReleaseGPUShader(dev, vs);
		SDL_ReleaseGPUShader(dev, ps);
		if (!parity_pipeline) {
			fprintf(stderr, "parity pipeline create failed: %s\n", SDL_GetError());
			return -1;
		}
	}

	/* fx pipeline: sprite_fx with the client's blend state */
	{
		SDL_GPUShader *vs =
		    load_shader_file("res/shaders/compiled/sprite_fx_vs.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 1);
		SDL_GPUShader *ps =
		    load_shader_file("res/shaders/compiled/sprite_fx_ps.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, 1);
		if (!vs || !ps) {
			return -1;
		}
		SDL_GPUVertexBufferDescription vb_desc = {
		    .slot = 0, .pitch = sizeof(quad_vertex_t), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX};
		SDL_GPUVertexAttribute attrs[2] = {
		    {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0},
		    {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 8},
		};
		SDL_GPUColorTargetDescription color_desc = {
		    .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
		    .blend_state = {.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
		        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
		        .color_blend_op = SDL_GPU_BLENDOP_ADD,
		        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
		        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
		        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
		        .enable_blend = true},
		};
		SDL_GPUGraphicsPipelineCreateInfo pi = {
		    .vertex_shader = vs,
		    .fragment_shader = ps,
		    .vertex_input_state = {.vertex_buffer_descriptions = &vb_desc,
		        .num_vertex_buffers = 1,
		        .vertex_attributes = attrs,
		        .num_vertex_attributes = 2},
		    .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		    .rasterizer_state = {.fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE},
		    .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1, .sample_mask = 0xFFFFFFFF},
		    .target_info = {.color_target_descriptions = &color_desc, .num_color_targets = 1},
		};
		fx_pipeline = SDL_CreateGPUGraphicsPipeline(dev, &pi);
		SDL_ReleaseGPUShader(dev, vs);
		SDL_ReleaseGPUShader(dev, ps);
		if (!fx_pipeline) {
			fprintf(stderr, "fx pipeline create failed: %s\n", SDL_GetError());
			return -1;
		}
	}

	SDL_GPUBufferCreateInfo vbi = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = sizeof(quad_vertices)};
	quad_vbo = SDL_CreateGPUBuffer(dev, &vbi);
	if (!quad_vbo || upload_buffer(quad_vbo, quad_vertices, sizeof(quad_vertices)) != 0) {
		return -1;
	}

	SDL_GPUBufferCreateInfo ibi = {
	    .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, .size = MAX_INST * sizeof(gpu_fx_instance_t)};
	inst_buf = SDL_CreateGPUBuffer(dev, &ibi);
	if (!inst_buf) {
		return -1;
	}

	SDL_GPUSamplerCreateInfo si = {
	    .min_filter = SDL_GPU_FILTER_NEAREST,
	    .mag_filter = SDL_GPU_FILTER_NEAREST,
	    .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
	    .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	    .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	    .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	};
	sampler = SDL_CreateGPUSampler(dev, &si);
	return sampler ? 0 : -1;
}

/* One string draw of a scene (logical coordinates, like sdl_drawtext). */
typedef struct scene_text {
	const char *text;
	RenderFont *font;
	int flags; /* TF_* underlay bits only (affect sizex growth) */
	int sx, sy; /* logical draw position */
	int r, g, b; /* 0..255 */
	int alpha; /* 0..255 draw alpha */
	int clipsx, clipsy, clipex, clipey; /* logical clip window */
	int tier2; /* 1: draw as one pre-composed quad (cached-string path) */
} scene_text_t;

/* mini atlas page for the batched side */
static SDL_GPUTexture *page_tex;
static int page_pen_x, page_pen_y, page_row_h;

static int page_insert(const uint32_t *pixels, int w, int h, int *out_x, int *out_y)
{
	if (page_pen_x + w > PAGE_SIZE) {
		page_pen_x = 0;
		page_pen_y += page_row_h + 1;
		page_row_h = 0;
	}
	if (page_pen_y + h > PAGE_SIZE || w > PAGE_SIZE) {
		return -1;
	}
	if (upload_texture_region(page_tex, pixels, page_pen_x, page_pen_y, w, h) != 0) {
		return -1;
	}
	*out_x = page_pen_x;
	*out_y = page_pen_y;
	page_pen_x += w + 1;
	if (h > page_row_h) {
		page_row_h = h;
	}
	return 0;
}

/* per-scene glyph cache (font ptr, char) -> page region */
typedef struct glyph_slot {
	RenderFont *font;
	unsigned char c;
	int x, y, w, h, advance;
	int state; /* 0 empty-slot, 1 ready, 2 empty-glyph */
} glyph_slot_t;
static glyph_slot_t glyph_slots[2048];
static int glyph_slot_count;

static glyph_slot_t *glyph_lookup(RenderFont *font, unsigned char c)
{
	for (int i = 0; i < glyph_slot_count; i++) {
		if (glyph_slots[i].font == font && glyph_slots[i].c == c) {
			return &glyph_slots[i];
		}
	}
	if (glyph_slot_count >= (int)(sizeof(glyph_slots) / sizeof(glyph_slots[0]))) {
		return NULL;
	}

	uint32_t buffer[TEXT_GLYPH_MAX_DIM * TEXT_GLYPH_MAX_DIM];
	text_glyph_mask_t m;
	glyph_slot_t *s = &glyph_slots[glyph_slot_count];

	if (!text_glyph_rasterize(font, c, sdl_scale, buffer, &m)) {
		return NULL;
	}
	s->font = font;
	s->c = c;
	s->advance = m.advance;
	if (m.w == 0) {
		s->state = 2;
	} else {
		if (page_insert(m.pixel, m.w, m.h, &s->x, &s->y) != 0) {
			return NULL;
		}
		s->w = m.w;
		s->h = m.h;
		s->state = 1;
	}
	glyph_slot_count++;
	return s;
}

/* Draw one scene through the given path and read back the target.
 * batched=0: today's path (whole-string textures via sprite_simple).
 * batched=1: the phase-2 path (glyph/tier2 instances via sprite_fx). */
static int render_scene(const scene_text_t *texts, int num_texts, int batched, uint32_t *out)
{
	int rc = -1;
	SDL_GPUTexture *target = create_texture(TARGET_W, TARGET_H, 1);
	SDL_GPUTexture *bg_tex = create_texture(TARGET_W, TARGET_H, 0);
	SDL_GPUTexture *string_tex[16] = {0};
	gpu_fx_instance_t *instances = calloc(MAX_INST, sizeof(gpu_fx_instance_t));
	int num_inst = 0;
	SDL_GPUTransferBuffer *down = NULL;
	SDL_GPUFence *fence = NULL;

	if (!target || !bg_tex || !instances || num_texts > 16) {
		goto out;
	}

	/* deterministic opaque background */
	{
		uint32_t *bg = malloc((size_t)TARGET_W * TARGET_H * sizeof(uint32_t));
		uint32_t seed = 0xC0FFEE11u;
		for (int i = 0; i < TARGET_W * TARGET_H; i++) {
			seed ^= seed << 13;
			seed ^= seed >> 17;
			seed ^= seed << 5;
			bg[i] = 0xFF000000u | (seed & 0x00FFFFFFu);
		}
		int ok = upload_texture_region(bg_tex, bg, 0, 0, TARGET_W, TARGET_H);
		free(bg);
		if (ok != 0) {
			goto out;
		}
	}

	/* prepare per-string resources + batched instances */
	for (int t = 0; t < num_texts; t++) {
		const scene_text_t *st = &texts[t];
		int sizex = 0, sizey = 0;
		uint32_t *pix = sdl_rendertext_to_pixels(st->text, st->font, (uint32_t)IRGBA(st->r, st->g, st->b, 255),
		    st->flags, &sizex, &sizey);
		if (!pix) {
			goto out;
		}

		if (!batched) {
			string_tex[t] = create_texture(sizex, sizey, 0);
			if (!string_tex[t] || upload_texture_region(string_tex[t], pix, 0, 0, sizex, sizey) != 0) {
				FREE(pix);
				goto out;
			}
		} else if (st->tier2) {
			/* tier 2: whole string pixels as ONE atlas quad */
			int ax, ay;
			if (page_insert(pix, sizex, sizey, &ax, &ay) != 0) {
				FREE(pix);
				goto out;
			}
			/* replicate the drawtext GPU clip math (logical space) */
			int logical_w = sizex / sdl_scale, logical_h = sizey / sdl_scale;
			int draw_x = st->sx, draw_y = st->sy, draw_w = logical_w, draw_h = logical_h, src_x = 0, src_y = 0;
			if (draw_x < st->clipsx) {
				int cl = st->clipsx - draw_x;
				src_x = cl;
				draw_w -= cl;
				draw_x = st->clipsx;
			}
			if (draw_y < st->clipsy) {
				int cl = st->clipsy - draw_y;
				src_y = cl;
				draw_h -= cl;
				draw_y = st->clipsy;
			}
			if (draw_x + draw_w > st->clipex) {
				draw_w = st->clipex - draw_x;
			}
			if (draw_y + draw_h > st->clipey) {
				draw_h = st->clipey - draw_y;
			}
			if (draw_w > 0 && draw_h > 0) {
				gpu_fx_instance_t *in = &instances[num_inst++];
				in->dest[0] = (float)(draw_x * sdl_scale);
				in->dest[1] = (float)(draw_y * sdl_scale);
				in->dest[2] = (float)(draw_w * sdl_scale);
				in->dest[3] = (float)(draw_h * sdl_scale);
				in->src[0] = ax + src_x * sdl_scale;
				in->src[1] = ay + src_y * sdl_scale;
				in->src[2] = draw_w * sdl_scale;
				in->src[3] = draw_h * sdl_scale;
				in->org_sz[0] = in->src[0];
				in->org_sz[1] = in->src[1];
				in->org_sz[2] = in->src[2];
				in->org_sz[3] = in->src[3];
				in->colorize[3] = GPU_FX_MODE_PLAIN;
				in->balance[0] = 255;
				in->balance[1] = 255;
				in->balance[2] = 255;
				in->light_b[0] = 15;
				in->light_b[1] = st->alpha;
			}
		} else {
			/* tier 1: per-glyph quads (alpha must be 255 - eligibility) */
			int pen = st->sx * sdl_scale;
			int pen_y = st->sy * sdl_scale;
			int cx0 = st->clipsx * sdl_scale, cy0 = st->clipsy * sdl_scale;
			int cx1 = st->clipex * sdl_scale, cy1 = st->clipey * sdl_scale;
			for (const char *c = st->text; *c && *c != RENDER_TEXT_TERMINATOR; c++) {
				if (*c < 0) {
					continue;
				}
				glyph_slot_t *g = glyph_lookup(st->font, (unsigned char)*c);
				if (!g) {
					FREE(pix);
					goto out;
				}
				if (g->state == 1) {
					text_glyph_mask_t m = {.w = g->w, .h = g->h, .advance = g->advance};
					int dx, dy, sx2, sy2, w, h;
					if (text_glyph_quad(&m, pen, pen_y, cx0, cy0, cx1, cy1, &dx, &dy, &sx2, &sy2, &w, &h)) {
						gpu_fx_instance_t *in = &instances[num_inst++];
						in->dest[0] = (float)dx;
						in->dest[1] = (float)dy;
						in->dest[2] = (float)w;
						in->dest[3] = (float)h;
						in->src[0] = g->x + sx2;
						in->src[1] = g->y + sy2;
						in->src[2] = w;
						in->src[3] = h;
						in->org_sz[0] = in->src[0];
						in->org_sz[1] = in->src[1];
						in->org_sz[2] = w;
						in->org_sz[3] = h;
						in->colorize[3] = GPU_FX_MODE_PLAIN;
						in->balance[0] = st->r;
						in->balance[1] = st->g;
						in->balance[2] = st->b;
						in->light_b[0] = 15;
						in->light_b[1] = 255;
					}
				}
				pen += g->advance;
			}
		}
		FREE(pix);
	}

	if (batched && num_inst > 0) {
		if (upload_buffer(inst_buf, instances, (size_t)num_inst * sizeof(gpu_fx_instance_t)) != 0) {
			goto out;
		}
	}

	SDL_GPUTransferBufferCreateInfo dni = {
	    .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = TARGET_W * TARGET_H * sizeof(uint32_t)};
	down = SDL_CreateGPUTransferBuffer(dev, &dni);
	if (!down) {
		goto out;
	}

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
	if (!cmd) {
		goto out;
	}

	SDL_GPUColorTargetInfo ct = {
	    .texture = target,
	    .clear_color = {0.2f, 0.1f, 0.3f, 1.0f},
	    .load_op = SDL_GPU_LOADOP_CLEAR,
	    .store_op = SDL_GPU_STOREOP_STORE,
	};
	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);

	/* background (identical on both paths) */
	{
		SDL_BindGPUGraphicsPipeline(pass, parity_pipeline);
		SDL_GPUBufferBinding vb = {.buffer = quad_vbo, .offset = 0};
		SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
		SDL_GPUTextureSamplerBinding tsb = {.texture = bg_tex, .sampler = sampler};
		SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
		parity_uniforms_t pu = {
		    .dest_x = 0,
		    .dest_y = 0,
		    .dest_w = TARGET_W,
		    .dest_h = TARGET_H,
		    .src_u = 0,
		    .src_v = 0,
		    .src_w = 1,
		    .src_h = 1,
		    .color_r = 1,
		    .color_g = 1,
		    .color_b = 1,
		    .color_a = 1,
		    .screen_w = TARGET_W,
		    .screen_h = TARGET_H,
		};
		SDL_PushGPUVertexUniformData(cmd, 0, &pu, sizeof(pu));
		SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
	}

	if (!batched) {
		/* whole-string textures through the parity pipeline (today's path) */
		for (int t = 0; t < num_texts; t++) {
			const scene_text_t *st = &texts[t];
			float tw, th;
			int sizex = 0, sizey = 0;
			uint32_t *pix = sdl_rendertext_to_pixels(st->text, st->font, (uint32_t)IRGBA(st->r, st->g, st->b, 255),
			    st->flags, &sizex, &sizey);
			if (!pix) {
				continue;
			}
			FREE(pix);
			tw = (float)sizex;
			th = (float)sizey;

			int logical_w = sizex / sdl_scale, logical_h = sizey / sdl_scale;
			int draw_x = st->sx, draw_y = st->sy, draw_w = logical_w, draw_h = logical_h, src_x = 0, src_y = 0;
			if (draw_x < st->clipsx) {
				int cl = st->clipsx - draw_x;
				src_x = cl;
				draw_w -= cl;
				draw_x = st->clipsx;
			}
			if (draw_y < st->clipsy) {
				int cl = st->clipsy - draw_y;
				src_y = cl;
				draw_h -= cl;
				draw_y = st->clipsy;
			}
			if (draw_x + draw_w > st->clipex) {
				draw_w = st->clipex - draw_x;
			}
			if (draw_y + draw_h > st->clipey) {
				draw_h = st->clipey - draw_y;
			}
			if (draw_w <= 0 || draw_h <= 0) {
				continue;
			}

			SDL_BindGPUGraphicsPipeline(pass, parity_pipeline);
			SDL_GPUBufferBinding vb = {.buffer = quad_vbo, .offset = 0};
			SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
			SDL_GPUTextureSamplerBinding tsb = {.texture = string_tex[t], .sampler = sampler};
			SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
			parity_uniforms_t pu = {
			    .dest_x = (float)(draw_x * sdl_scale),
			    .dest_y = (float)(draw_y * sdl_scale),
			    .dest_w = (float)(draw_w * sdl_scale),
			    .dest_h = (float)(draw_h * sdl_scale),
			    .src_u = (float)(src_x * sdl_scale) / tw,
			    .src_v = (float)(src_y * sdl_scale) / th,
			    .src_w = (float)(draw_w * sdl_scale) / tw,
			    .src_h = (float)(draw_h * sdl_scale) / th,
			    .color_r = 1,
			    .color_g = 1,
			    .color_b = 1,
			    .color_a = (float)st->alpha / 255.0f,
			    .screen_w = TARGET_W,
			    .screen_h = TARGET_H,
			};
			SDL_PushGPUVertexUniformData(cmd, 0, &pu, sizeof(pu));
			SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
		}
	} else if (num_inst > 0) {
		SDL_BindGPUGraphicsPipeline(pass, fx_pipeline);
		SDL_GPUBufferBinding vb = {.buffer = quad_vbo, .offset = 0};
		SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
		SDL_BindGPUVertexStorageBuffers(pass, 0, &inst_buf, 1);
		SDL_GPUTextureSamplerBinding tsb = {.texture = page_tex, .sampler = sampler};
		SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
		SDL_BindGPUFragmentStorageBuffers(pass, 0, &inst_buf, 1);
		gpu_fx_vs_uniforms_t vsu = {
		    .screen_w = TARGET_W,
		    .screen_h = TARGET_H,
		    .inv_screen_w = 1.0f / TARGET_W,
		    .inv_screen_h = 1.0f / TARGET_H,
		    .base_instance = 0,
		};
		SDL_PushGPUVertexUniformData(cmd, 0, &vsu, sizeof(vsu));
		gpu_fx_ps_uniforms_t psu = {.sdl_scale = sdl_scale, .le_bonus = 0, .base_instance = 0};
		SDL_PushGPUFragmentUniformData(cmd, 0, &psu, sizeof(psu));
		SDL_DrawGPUPrimitives(pass, 6, (Uint32)num_inst, 0, 0);
	}

	SDL_EndGPURenderPass(pass);

	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTextureRegion rreg = {.texture = target, .w = TARGET_W, .h = TARGET_H, .d = 1};
	SDL_GPUTextureTransferInfo dtt = {
	    .transfer_buffer = down, .offset = 0, .pixels_per_row = TARGET_W, .rows_per_layer = TARGET_H};
	SDL_DownloadFromGPUTexture(cp, &rreg, &dtt);
	SDL_EndGPUCopyPass(cp);

	fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
	if (!fence || !SDL_WaitForGPUFences(dev, true, &fence, 1)) {
		goto out;
	}
	void *m = SDL_MapGPUTransferBuffer(dev, down, false);
	if (!m) {
		goto out;
	}
	memcpy(out, m, (size_t)TARGET_W * TARGET_H * sizeof(uint32_t));
	SDL_UnmapGPUTransferBuffer(dev, down);
	rc = 0;

out:
	if (fence) {
		SDL_ReleaseGPUFence(dev, fence);
	}
	if (down) {
		SDL_ReleaseGPUTransferBuffer(dev, down);
	}
	for (int t = 0; t < 16; t++) {
		if (string_tex[t]) {
			SDL_ReleaseGPUTexture(dev, string_tex[t]);
		}
	}
	if (bg_tex) {
		SDL_ReleaseGPUTexture(dev, bg_tex);
	}
	if (target) {
		SDL_ReleaseGPUTexture(dev, target);
	}
	free(instances);
	return rc;
}

static void part_b_case(const char *name, const scene_text_t *texts, int num_texts, int tolerance)
{
	uint32_t *ref = malloc((size_t)TARGET_W * TARGET_H * sizeof(uint32_t));
	uint32_t *got = malloc((size_t)TARGET_W * TARGET_H * sizeof(uint32_t));

	if (render_scene(texts, num_texts, 0, ref) != 0 || render_scene(texts, num_texts, 1, got) != 0) {
		printf("B %-34s FAIL (render error: %s)\n", name, SDL_GetError());
		failures++;
		free(ref);
		free(got);
		return;
	}

	int max_delta = 0, over = 0, fx = -1, fy = -1;
	for (int i = 0; i < TARGET_W * TARGET_H; i++) {
		uint32_t e = ref[i], g = got[i];
		int d = abs((int)IGET_A(e) - (int)IGET_A(g));
		int dr = abs((int)IGET_R(e) - (int)IGET_R(g));
		int dg = abs((int)IGET_G(e) - (int)IGET_G(g));
		int db = abs((int)IGET_B(e) - (int)IGET_B(g));
		if (dr > d) {
			d = dr;
		}
		if (dg > d) {
			d = dg;
		}
		if (db > d) {
			d = db;
		}
		if (d > max_delta) {
			max_delta = d;
		}
		if (d > tolerance) {
			if (!over) {
				fx = i % TARGET_W;
				fy = i / TARGET_W;
			}
			over++;
		}
	}

	if (over) {
		printf("B %-34s FAIL (%d px over tol %d, max %d, first at %d,%d: ref %08x got %08x)\n", name, over, tolerance,
		    max_delta, fx, fy, ref[fx + fy * TARGET_W], got[fx + fy * TARGET_W]);
		failures++;
	} else {
		printf("B %-34s PASS (max delta %d, tol %d)\n", name, max_delta, tolerance);
	}
	free(ref);
	free(got);
}

static void run_part_b(void)
{
	char name[64];

	/* reset the scene page/glyph cache per scale (glyph rasters differ) */
	glyph_slot_count = 0;
	page_pen_x = page_pen_y = page_row_h = 0;

	/* 1: plain colored strings, all three sizes */
	{
		scene_text_t t[] = {
		    {"The quick brown fox 123", fonta, 0, 4, 4, 255, 255, 255, 255, 0, 0, TARGET_W, TARGET_H, 0},
		    {"jumps over the lazy dog", fontb, 0, 4, 20, 90, 220, 140, 255, 0, 0, TARGET_W, TARGET_H, 0},
		    {"HP 45/99 [Gold: 123456]", fontc, 0, 4, 34, 255, 210, 60, 255, 0, 0, TARGET_W, TARGET_H, 0},
		};
		snprintf(name, sizeof(name), "plain-3fonts-s%d", sdl_scale);
		part_b_case(name, t, 3, 0);
	}

	/* 2: shaded text = underlay pass + main pass, like render_text_alpha */
	{
		scene_text_t t[] = {
		    {"Shaded caption +42", fonta_shaded, TF_SHADED_FONT, 5, 5, 0, 0, 0, 255, 0, 0, TARGET_W, TARGET_H, 0},
		    {"Shaded caption +42", fonta, 0, 6, 6, 250, 250, 100, 255, 0, 0, TARGET_W, TARGET_H, 0},
		    {"Framed one", fontc_framed, TF_FRAMED_FONT, 5, 21, 0, 0, 0, 255, 0, 0, TARGET_W, TARGET_H, 0},
		    {"Framed one", fontc, 0, 6, 22, 255, 80, 80, 255, 0, 0, TARGET_W, TARGET_H, 0},
		};
		snprintf(name, sizeof(name), "shaded+framed-s%d", sdl_scale);
		part_b_case(name, t, 4, 0);
	}

	/* 3: clipped strings (all four edges cut) */
	{
		scene_text_t t[] = {
		    {"Clip me on every side!", fonta, 0, 10, 8, 255, 255, 255, 255, 14, 9, 60, 12, 0},
		    {"Second clipped line", fontb, 0, -5, 30, 200, 200, 255, 255, 0, 28, 40, 34, 0},
		};
		snprintf(name, sizeof(name), "clipped-s%d", sdl_scale);
		part_b_case(name, t, 2, 0);
	}

	/* 4: tier 2 - pre-composed string quads from the atlas, incl. alpha
	 * modulation (the cached-string fallback path: TTF strings, alpha
	 * fades). Tolerance 2: the parity shader discards alpha < 0.01 while
	 * the batched plain mode blends it (<= 2 LSB by construction). */
	{
		scene_text_t t[] = {
		    {"tier2 cached string", fonta, 0, 8, 6, 255, 255, 255, 255, 0, 0, TARGET_W, TARGET_H, 1},
		    {"fading text 50%", fontc, 0, 8, 20, 120, 255, 120, 128, 0, 0, TARGET_W, TARGET_H, 1},
		    {"clipped tier2", fontb, 0, 4, 40, 255, 255, 0, 200, 6, 41, 48, 45, 1},
		};
		snprintf(name, sizeof(name), "tier2-quads-s%d", sdl_scale);
		part_b_case(name, t, 3, 2);
	}
}

/* ========================================================================
 * Part C: TTF dual-outline-font equivalence (the FreeType I/O fix)
 * ======================================================================== */

#ifdef HAVE_TTF_COMPARE

#include <SDL3_ttf/SDL_ttf.h>

static SDL_Surface *to_argb(SDL_Surface *s)
{
	if (!s) {
		return NULL;
	}
	if (s->format == SDL_PIXELFORMAT_ARGB8888) {
		return s;
	}
	SDL_Surface *n = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
	SDL_DestroySurface(s);
	return n;
}

static int surfaces_equal(SDL_Surface *a, SDL_Surface *b)
{
	if (!a || !b || a->w != b->w || a->h != b->h) {
		return 0;
	}
	for (int y = 0; y < a->h; y++) {
		const uint32_t *ra = (const uint32_t *)((const uint8_t *)a->pixels + (size_t)y * a->pitch);
		const uint32_t *rb = (const uint32_t *)((const uint8_t *)b->pixels + (size_t)y * b->pitch);
		if (memcmp(ra, rb, (size_t)a->w * sizeof(uint32_t)) != 0) {
			return 0;
		}
	}
	return 1;
}

static void run_part_c(void)
{
	const char *font_path = "res/fonts/mplus-1m-bold.ttf";
	const char *strings[] = {
	    "The quick brown fox jumps over the lazy dog 0123456789",
	    "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ AVAWA Yj gpq",
	    "chat: [Clan] Somebody: hello there",
	};
	const float pts[] = {7.0f, 9.0f, 11.0f, 18.0f};
	const int outlines[] = {1, 2};
	SDL_Color col = {250, 240, 200, 255};
	int cases = 0, passed = 0;

	if (!TTF_Init()) {
		printf("C TTF SKIP (TTF_Init failed: %s)\n", SDL_GetError());
		return;
	}

	/* memory buffer for the "new" path */
	size_t mem_size = 0;
	void *mem = SDL_LoadFile(font_path, &mem_size);
	if (!mem) {
		printf("C TTF SKIP (cannot load %s)\n", font_path);
		TTF_Quit();
		return;
	}

	for (size_t p = 0; p < sizeof(pts) / sizeof(pts[0]); p++) {
		for (size_t o = 0; o < sizeof(outlines) / sizeof(outlines[0]); o++) {
			/* OLD: one file-backed handle, outline toggled per call
			 * (flushes the SDL_ttf glyph cache = per-frame FreeType I/O) */
			TTF_Font *old_font = TTF_OpenFont(font_path, pts[p]);
			/* NEW: dedicated outline handle on a memory face, outline set
			 * once at load */
			SDL_IOStream *io = SDL_IOFromConstMem(mem, mem_size);
			TTF_Font *new_font = io ? TTF_OpenFontIO(io, true, pts[p]) : NULL;
			if (!old_font || !new_font) {
				printf("C TTF FAIL (font open: %s)\n", SDL_GetError());
				failures++;
				if (old_font) {
					TTF_CloseFont(old_font);
				}
				if (new_font) {
					TTF_CloseFont(new_font);
				}
				continue;
			}
			TTF_SetFontHinting(old_font, TTF_HINTING_MONO);
			TTF_SetFontHinting(new_font, TTF_HINTING_MONO);
			TTF_SetFontOutline(new_font, outlines[o]);

			for (size_t s = 0; s < sizeof(strings) / sizeof(strings[0]); s++) {
				/* plain pass equivalence (memory face vs file face) */
				SDL_Surface *ref_plain = to_argb(TTF_RenderText_Blended(old_font, strings[s], 0, col));
				TTF_SetFontOutline(old_font, outlines[o]);
				SDL_Surface *ref_outline = to_argb(TTF_RenderText_Blended(old_font, strings[s], 0, col));
				TTF_SetFontOutline(old_font, 0);

				SDL_Surface *new_outline = to_argb(TTF_RenderText_Blended(new_font, strings[s], 0, col));
				TTF_SetFontOutline(new_font, 0);
				SDL_Surface *new_plain = to_argb(TTF_RenderText_Blended(new_font, strings[s], 0, col));
				TTF_SetFontOutline(new_font, outlines[o]);

				cases += 2;
				if (surfaces_equal(ref_plain, new_plain)) {
					passed++;
				} else {
					printf("C ttf-plain pt%.0f str%zu FAIL (surface mismatch)\n", (double)pts[p], s);
					failures++;
				}
				if (surfaces_equal(ref_outline, new_outline)) {
					passed++;
				} else {
					printf("C ttf-outline%d pt%.0f str%zu FAIL (surface mismatch)\n", outlines[o], (double)pts[p], s);
					failures++;
				}

				SDL_DestroySurface(ref_plain);
				SDL_DestroySurface(ref_outline);
				SDL_DestroySurface(new_plain);
				SDL_DestroySurface(new_outline);
			}

			TTF_CloseFont(old_font);
			TTF_CloseFont(new_font);
		}
	}

	SDL_free(mem);
	TTF_Quit();
	printf("C ttf-dual-font-equivalence         %s (%d/%d bit-exact)\n", (passed == cases) ? "PASS" : "FAIL", passed,
	    cases);
}

#else /* !HAVE_TTF_COMPARE */

static void run_part_c(void)
{
	printf("C ttf-dual-font-equivalence         SKIP (built without sdl3-ttf)\n");
}

#endif

/* ========================================================================
 * main
 * ======================================================================== */

/* ========================================================================
 * Part D: GPU-mode text cache hits
 *
 * Under the GPU renderer a cached string lives in gpu_tex (an atlas page or
 * a standalone texture) and tex stays NULL. The cache lookup used to demand
 * an SDL_Texture, so every text lookup missed and every visible string was
 * rasterized, uploaded and submitted again on every frame - the September
 * 2026 Windows slowdown. The stubbed gpu_texture_create hands out fake
 * handles while use_gpu_rendering is set, so this needs no GPU device.
 * ======================================================================== */

static void run_part_d(void)
{
	const char *text = "Military Standing - no rank yet";
	const char *other = "Honor 0";
	int color = 0x00FFFFFF;
	bool prev = use_gpu_rendering;
	int ok = 1;

	if (!sdl_init_for_tests()) {
		printf("D gpu-mode-text-cache-hit          SKIP (sdl_init_for_tests failed)\n");
		return;
	}
	use_gpu_rendering = true;

	long long hits_before = texc_hit;
	int first = sdl_tx_load(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, text, color, 0, fonta, 0, 0);
	int again = sdl_tx_load(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, text, color, 0, fonta, 0, 0);
	int different = sdl_tx_load(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, other, color, 0, fonta, 0, 0);
	int checkonly = sdl_tx_load(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, text, color, 0, fonta, 1, 0);

	use_gpu_rendering = prev;

	if (first == STX_NONE) {
		printf("D   first load returned STX_NONE\n");
		ok = 0;
	} else {
		if (!sdlt[first].gpu_tex || sdlt[first].tex) {
			printf("D   GPU entry has gpu_tex=%p tex=%p\n", (void *)sdlt[first].gpu_tex, (void *)sdlt[first].tex);
			ok = 0;
		}
		if (again != first) {
			printf("D   second lookup landed on slot %d, expected %d (cache MISS)\n", again, first);
			ok = 0;
		}
		if (texc_hit != hits_before + 1) {
			printf("D   texc_hit advanced by %lld, expected 1\n", texc_hit - hits_before);
			ok = 0;
		}
		if (different == first || different == STX_NONE) {
			printf("D   a different string shares slot %d\n", different);
			ok = 0;
		}
		if (checkonly != 1) {
			printf("D   checkonly lookup returned %d, expected 1\n", checkonly);
			ok = 0;
		}
	}
	printf("D gpu-mode-text-cache-hit          %s\n", ok ? "PASS" : "FAIL");
	if (!ok) {
		failures++;
	}
}

int main(void)
{
	/* the offscreen driver supports SDL_GPU (Vulkan) without a display;
	 * the dummy driver does not - fall back to it for part A/C only */
	if (!SDL_getenv("SDL_VIDEO_DRIVER")) {
		SDL_setenv_unsafe("SDL_VIDEO_DRIVER", "offscreen", 1);
	}
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_setenv_unsafe("SDL_VIDEO_DRIVER", "dummy", 1);
		if (!SDL_Init(SDL_INIT_VIDEO)) {
			printf("SKIP: SDL_Init failed (%s)\n", SDL_GetError());
			return 0;
		}
	}

	int have_gpu = (gpu_setup() == 0);
	if (have_gpu) {
		printf("GPU driver: %s\n\n", SDL_GetGPUDeviceDriver(dev));
		page_tex = create_texture(PAGE_SIZE, PAGE_SIZE, 0);
		if (!page_tex) {
			have_gpu = 0;
		} else {
			/* zero the page so uninitialized regions are transparent */
			uint32_t *zero = calloc((size_t)PAGE_SIZE * PAGE_SIZE, sizeof(uint32_t));
			upload_texture_region(page_tex, zero, 0, 0, PAGE_SIZE, PAGE_SIZE);
			free(zero);
		}
	} else {
		printf("NOTE: no SDL_GPU device (%s) - part B skipped\n\n", SDL_GetError());
	}

	for (int scale = 1; scale <= 2; scale++) {
		sdl_scale = scale;
		if (!build_fonts_for_scale()) {
			printf("NOTE: fonts for scale %d unavailable, skipping\n", scale);
			continue;
		}
		run_part_a();
		if (have_gpu) {
			run_part_b();
		} else {
			printf("B (scale %d)                        SKIP (no GPU device)\n", scale);
		}
		if (scale == 1) {
			run_part_d();
		}
	}

	run_part_c();

	printf("\n%s (%d failures)\n", failures ? "TEXT COMPARE FAILED" : "text compare passed", failures);
	return failures ? 1 : 0;
}
