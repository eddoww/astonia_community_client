/*
 * Shader-effects correctness harness.
 *
 * Renders the same sprite + effect combination through BOTH pipelines and
 * diffs the pixels:
 *   CPU: the real sdl_make() bake (sdl_image.c + sdl_effects.c) - the
 *        ground truth the game shipped with for two decades.
 *   GPU: base (effect-free) sdl_make() bake uploaded as a texture, then a
 *        real SDL_GPU draw through res/shaders/compiled/sprite_fx_{vs,ps}.spv
 *        with the effects passed as per-instance data.
 *
 * Effect mismatch is how the GPU redesign fails, so this checker exists
 * BEFORE the client integration and gates which effect combos the client
 * may route through the shader path.
 *
 * Tolerance: integer-only effect paths (lighting, color balance, freeze,
 * sink) must be bit-exact; paths that use doubles on the CPU (colorize,
 * shine) may differ by <= 2 LSB per channel.
 *
 * Needs a working SDL_GPU device (Vulkan/SPIRV). If none is available the
 * test SKIPs (exit 0) so `make test` stays green on GPU-less machines.
 *
 * Run from the repo root (shader binaries are loaded from
 * res/shaders/compiled/).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <SDL3/SDL.h>

#include "astonia.h"
#include "game/memory.h"
#include "sdl/sdl.h"
#include "sdl/sdl_private.h"
#include "sdl/sdl_gpu_shaderfx.h"

_Static_assert(sizeof(gpu_fx_instance_t) == 128, "gpu_fx_instance_t must be 128 bytes (std430 mirror)");

/* ========================================================================
 * Test case description
 * ======================================================================== */

typedef struct fx_case {
	const char *name;
	int scale_factor; /* sdl_scale for this case (1, 2) */
	int pattern; /* synthetic image pattern id */
	uint32_t sprite; /* controls colorize algorithm selection */
	/* effect params - raw struct sdl_texture values */
	int sink, freeze;
	int cr, cg, cb, light, sat;
	int c1, c2, c3, shine;
	int ml, ll, rl, ul, dl;
	uint64_t options; /* game_options for the case (GO_LIGHTER etc.) */
	int tolerance; /* max per-channel LSB delta allowed */
} fx_case_t;

#define LOGICAL_W 40
#define LOGICAL_H 40

/* ========================================================================
 * Synthetic sprite images
 * ======================================================================== */

static uint32_t xs_state;
static uint32_t xs_next(void)
{
	uint32_t x = xs_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	xs_state = x;
	return x;
}

/* Build a synthetic image at the current sdl_scale. The pixel buffer is
 * (xres*scale) x (yres*scale) like a real loaded sprite; we generate
 * arbitrary per-texel data (a superset of what the loader's block
 * upscaling produces). */
static void make_image(struct sdl_image *si, int pattern)
{
	int w = LOGICAL_W * sdl_scale;
	int h = LOGICAL_H * sdl_scale;
	int x, y;

	memset(si, 0, sizeof(*si));
	si->flags = 1;
	si->xres = LOGICAL_W;
	si->yres = LOGICAL_H;
	si->xoff = 0;
	si->yoff = 0;
	/* astonia.h defines SDL_FAST_MALLOC, so sdl_make allocates st->pixel
	 * with MALLOC - use the same allocator family throughout */
	si->pixel = MALLOC((size_t)w * h * sizeof(uint32_t));

	xs_state = 0x12345678u + (uint32_t)pattern;

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			int r = 0, g = 0, b = 0, a = 255;
			int band = (y * 8) / h; /* eight horizontal bands */

			switch (pattern) {
			case 0: /* colorize-sensitive bands + greys + noise */
				switch (band) {
				case 0: /* green-dominant ramp (colorizable ch1) */
					g = 32 + (x * 223) / w;
					r = (g * 3) / 8;
					b = (g * 4) / 8;
					break;
				case 1: /* blue-dominant ramp (colorizable ch2) */
					b = 32 + (x * 223) / w;
					r = (b * 5) / 8;
					g = (b * 3) / 8;
					break;
				case 2: /* red-dominant ramp (colorizable ch3) */
					r = 32 + (x * 223) / w;
					g = (r * 2) / 8;
					b = (r * 1) / 8;
					break;
				case 3: /* greys */
					r = g = b = (x * 255) / w;
					break;
				case 4: /* near colorize thresholds */
					g = 200;
					r = (x * 160) / w; /* crosses 0.7 ratio */
					b = 100;
					break;
				case 5: /* saturated mix */
					r = 255 - (x * 255) / w;
					g = (x * 255) / w;
					b = 128;
					break;
				case 6: /* random */
					r = (int)(xs_next() & 255);
					g = (int)(xs_next() & 255);
					b = (int)(xs_next() & 255);
					break;
				default: /* alpha variations over a ramp */
					r = 180;
					g = 90;
					b = (x * 255) / w;
					a = (x % 4 == 0) ? 0 : (x % 4 == 1) ? 64 : (x % 4 == 2) ? 128 : 255;
					break;
				}
				break;
			case 1: /* mostly random with transparent holes */
				r = (int)(xs_next() & 255);
				g = (int)(xs_next() & 255);
				b = (int)(xs_next() & 255);
				a = ((xs_next() & 7) == 0) ? 0 : 255;
				break;
			default: /* flat mid grey */
				r = g = b = 128;
				break;
			}

			if (a == 0) {
				r = g = b = 0; /* loader zeroes fully transparent pixels */
			}
			si->pixel[x + y * w] = IRGBA(r, g, b, a);
		}
	}
}

/* ========================================================================
 * CPU reference bake
 * ======================================================================== */

static void cpu_bake(struct sdl_texture *st, struct sdl_image *si, const fx_case_t *c, int neutral)
{
	memset(st, 0, sizeof(*st));
	st->sprite = c->sprite;
	st->scale = 100;
	if (!neutral) {
		st->sink = (int8_t)c->sink;
		st->freeze = (uint8_t)c->freeze;
		st->cr = (int16_t)c->cr;
		st->cg = (int16_t)c->cg;
		st->cb = (int16_t)c->cb;
		st->light = (int16_t)c->light;
		st->sat = (int16_t)c->sat;
		st->c1 = (uint16_t)c->c1;
		st->c2 = (uint16_t)c->c2;
		st->c3 = (uint16_t)c->c3;
		st->shine = (uint16_t)c->shine;
		st->ml = (int8_t)c->ml;
		st->ll = (int8_t)c->ll;
		st->rl = (int8_t)c->rl;
		st->ul = (int8_t)c->ul;
		st->dl = (int8_t)c->dl;
	} else {
		/* base texture: no effects, neutral (identity) lighting */
		st->ml = st->ll = st->rl = st->ul = st->dl = 15;
	}

	sdl_make(st, si, 1); /* alloc */
	sdl_make(st, si, 2); /* bake */
}

/* ========================================================================
 * GPU side
 * ======================================================================== */

static SDL_GPUDevice *dev;
static SDL_GPUGraphicsPipeline *pipeline;
static SDL_GPUBuffer *quad_vbo;
static SDL_GPUBuffer *inst_buf;
static SDL_GPUSampler *sampler;

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

static SDL_GPUShader *load_shader_file(const char *path, SDL_GPUShaderStage stage, Uint32 num_samplers,
    Uint32 num_storage, Uint32 num_uniform)
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

static int gpu_setup(void)
{
	dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, NULL);
	if (!dev) {
		return -1;
	}

	SDL_GPUShader *vs = load_shader_file("res/shaders/compiled/sprite_fx_vs.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 1);
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
	    /* blending intentionally disabled: we compare the raw shader
	     * output against the CPU-baked texture content (pre-blend) */
	    .blend_state = {.enable_blend = false},
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
	pipeline = SDL_CreateGPUGraphicsPipeline(dev, &pi);
	SDL_ReleaseGPUShader(dev, vs);
	SDL_ReleaseGPUShader(dev, ps);
	if (!pipeline) {
		fprintf(stderr, "pipeline create failed: %s\n", SDL_GetError());
		return -1;
	}

	SDL_GPUBufferCreateInfo vbi = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = sizeof(quad_vertices)};
	quad_vbo = SDL_CreateGPUBuffer(dev, &vbi);
	if (!quad_vbo || upload_buffer(quad_vbo, quad_vertices, sizeof(quad_vertices)) != 0) {
		return -1;
	}

	SDL_GPUBufferCreateInfo ibi = {
	    .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, .size = sizeof(gpu_fx_instance_t)};
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
	if (!sampler) {
		return -1;
	}
	return 0;
}

/* Render one sprite through the shader path and read the pixels back.
 * base: base pixel data (w x h texels), out: w*h uint32 ARGB. */
static int gpu_render(const uint32_t *base, int w, int h, const gpu_fx_instance_t *inst, int le_bonus, uint32_t *out)
{
	int rc = -1;
	SDL_GPUTexture *tex = NULL, *target = NULL;
	SDL_GPUTransferBuffer *up = NULL, *down = NULL;
	SDL_GPUFence *fence = NULL;
	size_t size = (size_t)w * h * sizeof(uint32_t);

	SDL_GPUTextureCreateInfo tci = {
	    .type = SDL_GPU_TEXTURETYPE_2D,
	    .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
	    .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
	    .width = (Uint32)w,
	    .height = (Uint32)h,
	    .layer_count_or_depth = 1,
	    .num_levels = 1,
	    .sample_count = SDL_GPU_SAMPLECOUNT_1,
	};
	tex = SDL_CreateGPUTexture(dev, &tci);

	SDL_GPUTextureCreateInfo rti = tci;
	rti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	target = SDL_CreateGPUTexture(dev, &rti);

	SDL_GPUTransferBufferCreateInfo upi = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = (Uint32)size};
	up = SDL_CreateGPUTransferBuffer(dev, &upi);
	SDL_GPUTransferBufferCreateInfo dni = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = (Uint32)size};
	down = SDL_CreateGPUTransferBuffer(dev, &dni);

	if (!tex || !target || !up || !down) {
		goto out;
	}

	void *m = SDL_MapGPUTransferBuffer(dev, up, false);
	if (!m) {
		goto out;
	}
	memcpy(m, base, size);
	SDL_UnmapGPUTransferBuffer(dev, up);

	if (upload_buffer(inst_buf, inst, sizeof(*inst)) != 0) {
		goto out;
	}

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
	if (!cmd) {
		goto out;
	}

	/* upload base texture */
	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTextureTransferInfo tti = {.transfer_buffer = up, .offset = 0, .pixels_per_row = (Uint32)w,
	    .rows_per_layer = (Uint32)h};
	SDL_GPUTextureRegion reg = {.texture = tex, .w = (Uint32)w, .h = (Uint32)h, .d = 1};
	SDL_UploadToGPUTexture(cp, &tti, &reg, false);
	SDL_EndGPUCopyPass(cp);

	/* render */
	SDL_GPUColorTargetInfo ct = {
	    .texture = target,
	    .clear_color = {0, 0, 0, 0},
	    .load_op = SDL_GPU_LOADOP_CLEAR,
	    .store_op = SDL_GPU_STOREOP_STORE,
	};
	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
	SDL_BindGPUGraphicsPipeline(pass, pipeline);
	SDL_GPUBufferBinding vb = {.buffer = quad_vbo, .offset = 0};
	SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
	SDL_BindGPUVertexStorageBuffers(pass, 0, &inst_buf, 1);
	SDL_GPUTextureSamplerBinding tsb = {.texture = tex, .sampler = sampler};
	SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
	SDL_BindGPUFragmentStorageBuffers(pass, 0, &inst_buf, 1);

	gpu_fx_vs_uniforms_t vsu = {
	    .screen_w = (float)w,
	    .screen_h = (float)h,
	    .inv_screen_w = 1.0f / (float)w,
	    .inv_screen_h = 1.0f / (float)h,
	    .base_instance = 0,
	};
	SDL_PushGPUVertexUniformData(cmd, 0, &vsu, sizeof(vsu));
	gpu_fx_ps_uniforms_t psu = {.sdl_scale = sdl_scale, .le_bonus = le_bonus, .base_instance = 0};
	SDL_PushGPUFragmentUniformData(cmd, 0, &psu, sizeof(psu));

	SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
	SDL_EndGPURenderPass(pass);

	/* read back */
	cp = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTextureRegion rreg = {.texture = target, .w = (Uint32)w, .h = (Uint32)h, .d = 1};
	SDL_GPUTextureTransferInfo dtt = {.transfer_buffer = down, .offset = 0, .pixels_per_row = (Uint32)w,
	    .rows_per_layer = (Uint32)h};
	SDL_DownloadFromGPUTexture(cp, &rreg, &dtt);
	SDL_EndGPUCopyPass(cp);

	fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
	if (!fence || !SDL_WaitForGPUFences(dev, true, &fence, 1)) {
		goto out;
	}

	m = SDL_MapGPUTransferBuffer(dev, down, false);
	if (!m) {
		goto out;
	}
	memcpy(out, m, size);
	SDL_UnmapGPUTransferBuffer(dev, down);
	rc = 0;

out:
	if (fence) {
		SDL_ReleaseGPUFence(dev, fence);
	}
	if (up) {
		SDL_ReleaseGPUTransferBuffer(dev, up);
	}
	if (down) {
		SDL_ReleaseGPUTransferBuffer(dev, down);
	}
	if (tex) {
		SDL_ReleaseGPUTexture(dev, tex);
	}
	if (target) {
		SDL_ReleaseGPUTexture(dev, target);
	}
	return rc;
}

/* ========================================================================
 * Comparison
 * ======================================================================== */

typedef struct diff_stats {
	int max_delta;
	int over_tolerance;
	int compared;
	int first_bad_x, first_bad_y;
	uint32_t first_bad_exp, first_bad_got;
} diff_stats_t;

static void diff_pixels(const uint32_t *exp, const uint32_t *got, int w, int h, int tolerance, diff_stats_t *st)
{
	memset(st, 0, sizeof(*st));
	st->first_bad_x = -1;

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			uint32_t e = exp[x + y * w], g = got[x + y * w];
			int ea = (int)IGET_A(e), ga = (int)IGET_A(g);

			if (ea == 0 && ga == 0) {
				continue; /* both invisible - RGB is meaningless */
			}
			st->compared++;

			int d = abs(ea - ga);
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

			if (d > st->max_delta) {
				st->max_delta = d;
			}
			if (d > tolerance) {
				if (st->over_tolerance == 0) {
					st->first_bad_x = x;
					st->first_bad_y = y;
					st->first_bad_exp = e;
					st->first_bad_got = g;
				}
				st->over_tolerance++;
			}
		}
	}
}

/* ========================================================================
 * Cases
 * ======================================================================== */

/* clang-format off */
static const fx_case_t cases[] = {
	/* name                     s  pat sprite   sink fz  cr   cg   cb  light sat  c1     c2     c3     shine ml ll rl ul dl  options   tol */
	{"identity",                1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"uniform-light-0",         1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,     0, 0, 0, 0, 0, 0,        0},
	{"uniform-light-1",         1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,     1, 1, 1, 1, 1, 0,        0},
	{"uniform-light-4",         1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,     4, 4, 4, 4, 4, 0,        0},
	{"uniform-light-8",         1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,     8, 8, 8, 8, 8, 0,        0},
	{"uniform-light-12",        1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,    12,12,12,12,12, 0,        0},
	{"uniform-light-8-lighter", 1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,     8, 8, 8, 8, 8, GO_LIGHTER, 0},
	{"uniform-light-8-l2",      1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,     8, 8, 8, 8, 8, GO_LIGHTER|GO_LIGHTER2, 0},
	{"dir-light-a",             1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,    12, 4,15, 8, 2, 0,        0},
	{"dir-light-b",             1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,     8,15, 0,15, 0, 0,        0},
	{"dir-light-c",             1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,     0,15,15,15,15, 0,        0},
	{"dir-light-scale2",        2, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,    12, 4,15, 8, 2, 0,        0},
	{"colorize-c1",             1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0x7C00,0,     0,     0,    15,15,15,15,15, 0,        2},
	{"colorize-c2",             1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0x03E0,0,     0,    15,15,15,15,15, 0,        2},
	{"colorize-c3",             1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0x001F,0,    15,15,15,15,15, 0,        2},
	{"colorize-all",            1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0x7C00,0x03E0,0x001F,0,    15,15,15,15,15, 0,        2},
	{"colorize-mixed-vals",     1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0x2E85,0x1D07,0x5432,0,    15,15,15,15,15, 0,        2},
	{"colorize-shinebit-c1",    1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0xAE85,0,     0,     0,    15,15,15,15,15, 0,        2},
	{"colorize-new-c1",         1, 0,  220001,  0,   0,  0,   0,   0,  0,    0,   0x2E85,0,     0,     0,    15,15,15,15,15, 0,        2},
	{"colorize-new-all",        1, 0,  220001,  0,   0,  0,   0,   0,  0,    0,   0x2E85,0x1D07,0x5432,0,    15,15,15,15,15, 0,        2},
	{"colorize-new-scale2",     2, 0,  220001,  0,   0,  0,   0,   0,  0,    0,   0x2E85,0x1D07,0x5432,0,    15,15,15,15,15, 0,        2},
	{"colorize-rand",           1, 1,  1000,    0,   0,  0,   0,   0,  0,    0,   0x2E85,0x1D07,0x5432,0,    15,15,15,15,15, 0,        2},
	{"colorize-new-rand",       1, 1,  220001,  0,   0,  0,   0,   0,  0,    0,   0x2E85,0x1D07,0x5432,0,    15,15,15,15,15, 0,        2},
	{"balance-gilded",          1, 0,  1000,    0,   0,  50,  50,  0,  0,    0,   0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"balance-neg",             1, 0,  1000,    0,   0, -80, -40, -120,0,    0,   0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"balance-light-pos",       1, 0,  1000,    0,   0,  0,   0,   0,  60,   0,   0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"balance-light-neg",       1, 0,  1000,    0,   0,  0,   0,   0, -60,   0,   0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"balance-sat",             1, 0,  1000,    0,   0,  0,   0,   0,  0,    14,  0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"balance-full",            1, 0,  1000,    0,   0,  90, -50,  70, -20,  8,   0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"shine-25",                1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     25,   15,15,15,15,15, 0,        2},
	{"shine-60",                1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     60,   15,15,15,15,15, 0,        2},
	{"shine-100",               1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0,     0,     0,     100,  15,15,15,15,15, 0,        2},
	{"freeze-1",                1, 0,  1000,    0,   1,  0,   0,   0,  0,    0,   0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"freeze-4",                1, 0,  1000,    0,   4,  0,   0,   0,  0,    0,   0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"freeze-7",                1, 0,  1000,    0,   7,  0,   0,   0,  0,    0,   0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"sink-6",                  1, 0,  1000,    6,   0,  0,   0,   0,  0,    0,   0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"sink-20",                 1, 0,  1000,    20,  0,  0,   0,   0,  0,    0,   0,     0,     0,     0,    15,15,15,15,15, 0,        0},
	{"combo-item-tint",         1, 0,  1000,    0,   0,  50,  50,  0,  0,    0,   0,     0,     0,     40,    8, 4,12, 8, 8, 0,        2},
	{"combo-char",              1, 0,  1000,    0,   0,  0,   0,   0,  0,    0,   0x2E85,0x1D07,0x5432,0,    10, 6,14,10, 6, 0,        2},
	{"combo-frozen-lit",        1, 0,  1000,    0,   5, -30,  0,  30,  10,   6,   0,     0,     0,     0,     6, 2,10, 6, 6, 0,        2},
	{"combo-everything",        1, 0,  1000,    4,   3,  40, -20,  10, -10,  4,   0x2E85,0,     0x5432,30,    9, 3,13, 9, 5, 0,        2},
	{"combo-everything-new",    1, 1,  220001,  4,   3,  40, -20,  10, -10,  4,   0x2E85,0,     0x5432,30,    9, 3,13, 9, 5, 0,        2},
	{"combo-scale2",            2, 0,  1000,    0,   0,  50,  50,  0,  0,    0,   0x2E85,0,     0,     0,     8, 4,12, 8, 8, 0,        2},
};
/* clang-format on */

#define NUM_CASES ((int)(sizeof(cases) / sizeof(cases[0])))

/* ========================================================================
 * main
 * ======================================================================== */

int main(void)
{
	int failed = 0, exact = 0, tolerant = 0;

	/* No window and no renderer are needed. The offscreen video driver
	 * supports SDL_GPU (Vulkan) without a display; the dummy driver does
	 * not - fall back to it only to fail the device probe gracefully. */
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
	if (gpu_setup() != 0) {
		printf("SKIP: no usable SDL_GPU device (%s)\n", SDL_GetError());
		return 0;
	}
	printf("GPU driver: %s\n", SDL_GetGPUDeviceDriver(dev));

	printf("\n%-26s %5s %8s %9s %s\n", "case", "tol", "maxdiff", "compared", "result");
	printf("--------------------------------------------------------------------\n");

	for (int i = 0; i < NUM_CASES; i++) {
		const fx_case_t *c = &cases[i];
		struct sdl_image si;
		struct sdl_texture st_full, st_base;
		diff_stats_t d;

		sdl_scale = c->scale_factor;
		game_options = c->options;

		make_image(&si, c->pattern);
		cpu_bake(&st_full, &si, c, 0);
		cpu_bake(&st_base, &si, c, 1);

		int w = st_base.xres * sdl_scale;
		int h = st_base.yres * sdl_scale;

		/* build the instance exactly like the client will */
		int sink = c->sink ? ((c->sink < st_full.yres - 4) ? c->sink : ((st_full.yres - 4 > 0) ? st_full.yres - 4 : 0))
		                   : 0;
		gpu_fx_instance_t inst = {
		    .dest = {0.0f, 0.0f, (float)w, (float)h},
		    .src = {0, 0, w, h},
		    .org_sz = {0, 0, w, h},
		    .colorize = {(uint32_t)c->c1, (uint32_t)c->c2, (uint32_t)c->c3,
		        (c->sprite >= 220000) ? GPU_FX_COLORIZE_NEW : 0u},
		    .balance = {c->cr, c->cg, c->cb, c->light},
		    .fx = {c->sat, c->shine, c->freeze, sink * sdl_scale},
		    .light_a = {c->ml, c->ll, c->rl, c->ul},
		    .light_b = {c->dl, 255, 0, 0},
		};
		int le_bonus = ((c->options & GO_LIGHTER) ? 8 : 0) + ((c->options & GO_LIGHTER2) ? 12 : 0);

		uint32_t *got = MALLOC((size_t)w * h * sizeof(uint32_t));
		if (gpu_render(st_base.pixel, w, h, &inst, le_bonus, got) != 0) {
			printf("%-26s GPU RENDER FAILED: %s\n", c->name, SDL_GetError());
			failed++;
		} else {
			diff_pixels(st_full.pixel, got, w, h, c->tolerance, &d);
			const char *verdict;
			if (d.over_tolerance) {
				verdict = "FAIL";
				failed++;
			} else if (d.max_delta == 0) {
				verdict = "PASS (exact)";
				exact++;
			} else {
				verdict = "PASS";
				tolerant++;
			}
			printf("%-26s %5d %8d %9d %s\n", c->name, c->tolerance, d.max_delta, d.compared, verdict);
			if (d.over_tolerance) {
				printf("    %d px over tolerance; first at (%d,%d): expected %08x got %08x\n", d.over_tolerance,
				    d.first_bad_x, d.first_bad_y, d.first_bad_exp, d.first_bad_got);
			}
		}

		FREE(got);
		FREE(st_full.pixel);
		FREE(st_base.pixel);
		FREE(si.pixel);
	}

	printf("--------------------------------------------------------------------\n");
	printf("%d cases: %d exact, %d within tolerance, %d FAILED\n", NUM_CASES, exact, tolerant, failed);

	return failed ? 1 : 0;
}
