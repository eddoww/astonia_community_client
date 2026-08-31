/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Shader Effects (experimental, opt-in)
 *
 * Draws base (effect-free) sprite textures with the full game effect
 * pipeline applied in the fragment shader (res/shaders/sprite_fx.*),
 * batched with instanced rendering.
 *
 * Differences from the bypassed first-generation batcher (retired in
 * phase 2; it submitted an upload command buffer and BLOCKED on a fence
 * at every flush, which is why it lost to the unbatched path):
 *  - NO mid-frame fence waits. Instance data is written into a mapped
 *    transfer buffer as draws are recorded; ONE upload command buffer
 *    is submitted at frame end BEFORE the main render command buffer,
 *    and SDL_GPU's submission ordering + automatic hazard tracking
 *    orders the copy against the draws.
 *  - Ring of instance/transfer buffers (one per frame in flight) so a
 *    frame never overwrites data the GPU may still be reading.
 *  - Draws reference their instance range via a base-instance uniform
 *    (portable across backends - D3D12's SV_InstanceID excludes the
 *    first-instance draw parameter).
 *
 * Correctness of the shader effect pipeline is enforced by
 * tests/test_shaderfx_compare.c (CPU bake vs shader output).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "astonia.h"
#include "sdl/sdl.h"
#include "sdl/sdl_private.h"
#include "sdl/sdl_gpu.h"
#include "sdl/sdl_gpu_shaderfx.h"

_Static_assert(sizeof(gpu_fx_instance_t) == 128, "gpu_fx_instance_t must be 128 bytes (matches std430 struct)");

bool gpu_shaderfx_requested = false;

#define FX_MAX_INSTANCES 16384
#define FX_RING          3

typedef struct fx_vertex {
	float x, y, u, v;
} fx_vertex_t;

static const fx_vertex_t quad_vertices[6] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 1.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    {0.0f, 1.0f, 0.0f, 1.0f},
};

static struct {
	SDL_GPUGraphicsPipeline *pipeline;
	SDL_GPUBuffer *quad_vbo;
	SDL_GPUSampler *sampler;

	SDL_GPUBuffer *inst_buf[FX_RING];
	SDL_GPUTransferBuffer *inst_transfer[FX_RING];
	int ring; /* slot used this frame */

	gpu_fx_instance_t *mapped; /* mapped transfer of current slot */
	int count; /* instances written this frame */
	int range_start; /* first instance of the pending run */
	SDL_GPUTexture *run_texture; /* texture of the pending run */

	bool active; /* pipeline ready, path enabled */
	bool in_frame;

	/* per-frame stats (reset in frame_begin) */
	int stat_draws;
	int stat_sprites;
	int stat_flush_tex;
	int stat_flush_direct;
} fx = {0};

/* ==================================================================== */
/* setup                                                                */
/* ==================================================================== */

static SDL_GPUShader *fx_load_shader(
    const char *path, SDL_GPUShaderStage stage, Uint32 num_samplers, Uint32 num_storage, Uint32 num_uniform)
{
	size_t size;
	void *code = SDL_LoadFile(path, &size);
	if (!code) {
		note("gpu_shaderfx: cannot load %s", path);
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
	SDL_GPUShader *sh = SDL_CreateGPUShader(sdlgpu, &info);
	SDL_free(code);
	if (!sh) {
		note("gpu_shaderfx: shader create failed for %s: %s", path, SDL_GetError());
	}
	return sh;
}

static bool fx_create_quad_vbo(void)
{
	SDL_GPUBufferCreateInfo info = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = sizeof(quad_vertices)};
	fx.quad_vbo = SDL_CreateGPUBuffer(sdlgpu, &info);
	if (!fx.quad_vbo) {
		return false;
	}

	SDL_GPUTransferBufferCreateInfo ti = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = sizeof(quad_vertices)};
	SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(sdlgpu, &ti);
	if (!tb) {
		return false;
	}
	void *m = SDL_MapGPUTransferBuffer(sdlgpu, tb, false);
	if (!m) {
		SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
		return false;
	}
	memcpy(m, quad_vertices, sizeof(quad_vertices));
	SDL_UnmapGPUTransferBuffer(sdlgpu, tb);

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
	if (!cmd) {
		SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
		return false;
	}
	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTransferBufferLocation src = {.transfer_buffer = tb, .offset = 0};
	SDL_GPUBufferRegion dst = {.buffer = fx.quad_vbo, .offset = 0, .size = sizeof(quad_vertices)};
	SDL_UploadToGPUBuffer(cp, &src, &dst, false);
	SDL_EndGPUCopyPass(cp);
	SDL_SubmitGPUCommandBuffer(cmd);
	SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
	return true;
}

static bool fx_create_pipeline(void)
{
	if (!(SDL_GetGPUShaderFormats(sdlgpu) & SDL_GPU_SHADERFORMAT_SPIRV)) {
		note("gpu_shaderfx: backend has no SPIR-V support (only .spv shaders are shipped for the fx path yet)");
		return false;
	}

	SDL_GPUShader *vs = fx_load_shader("res/shaders/compiled/sprite_fx_vs.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 1);
	SDL_GPUShader *ps = fx_load_shader("res/shaders/compiled/sprite_fx_ps.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, 1);
	if (!vs || !ps) {
		if (vs) {
			SDL_ReleaseGPUShader(sdlgpu, vs);
		}
		if (ps) {
			SDL_ReleaseGPUShader(sdlgpu, ps);
		}
		return false;
	}

	SDL_GPUVertexBufferDescription vb_desc = {
	    .slot = 0, .pitch = sizeof(fx_vertex_t), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX};
	SDL_GPUVertexAttribute attrs[2] = {
	    {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0},
	    {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 8},
	};

	/* standard BLEND, like the parity sprite pipeline's default mode */
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

	SDL_GPUGraphicsPipelineCreateInfo info = {
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

	fx.pipeline = SDL_CreateGPUGraphicsPipeline(sdlgpu, &info);
	SDL_ReleaseGPUShader(sdlgpu, vs);
	SDL_ReleaseGPUShader(sdlgpu, ps);
	if (!fx.pipeline) {
		note("gpu_shaderfx: pipeline create failed: %s", SDL_GetError());
		return false;
	}
	return true;
}

bool gpu_shaderfx_init(void)
{
	if (!use_gpu_rendering || !sdlgpu) {
		return false;
	}
	if (fx.active) {
		return true;
	}

	memset(&fx, 0, sizeof(fx));

	if (!fx_create_pipeline()) {
		gpu_shaderfx_shutdown();
		return false;
	}
	if (!fx_create_quad_vbo()) {
		gpu_shaderfx_shutdown();
		return false;
	}

	SDL_GPUSamplerCreateInfo si = {
	    .min_filter = SDL_GPU_FILTER_NEAREST,
	    .mag_filter = SDL_GPU_FILTER_NEAREST,
	    .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
	    .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	    .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	    .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	};
	fx.sampler = SDL_CreateGPUSampler(sdlgpu, &si);
	if (!fx.sampler) {
		gpu_shaderfx_shutdown();
		return false;
	}

	for (int i = 0; i < FX_RING; i++) {
		SDL_GPUBufferCreateInfo bi = {
		    .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, .size = FX_MAX_INSTANCES * sizeof(gpu_fx_instance_t)};
		fx.inst_buf[i] = SDL_CreateGPUBuffer(sdlgpu, &bi);

		SDL_GPUTransferBufferCreateInfo ti = {
		    .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = FX_MAX_INSTANCES * sizeof(gpu_fx_instance_t)};
		fx.inst_transfer[i] = SDL_CreateGPUTransferBuffer(sdlgpu, &ti);

		if (!fx.inst_buf[i] || !fx.inst_transfer[i]) {
			note("gpu_shaderfx: instance ring alloc failed: %s", SDL_GetError());
			gpu_shaderfx_shutdown();
			return false;
		}
	}

	fx.active = true;
	return true;
}

void gpu_shaderfx_shutdown(void)
{
	if (!sdlgpu) {
		memset(&fx, 0, sizeof(fx));
		return;
	}
	if (fx.mapped) {
		SDL_UnmapGPUTransferBuffer(sdlgpu, fx.inst_transfer[fx.ring]);
		fx.mapped = NULL;
	}
	if (fx.pipeline) {
		SDL_ReleaseGPUGraphicsPipeline(sdlgpu, fx.pipeline);
	}
	if (fx.quad_vbo) {
		SDL_ReleaseGPUBuffer(sdlgpu, fx.quad_vbo);
	}
	if (fx.sampler) {
		SDL_ReleaseGPUSampler(sdlgpu, fx.sampler);
	}
	for (int i = 0; i < FX_RING; i++) {
		if (fx.inst_buf[i]) {
			SDL_ReleaseGPUBuffer(sdlgpu, fx.inst_buf[i]);
		}
		if (fx.inst_transfer[i]) {
			SDL_ReleaseGPUTransferBuffer(sdlgpu, fx.inst_transfer[i]);
		}
	}
	memset(&fx, 0, sizeof(fx));
}

bool gpu_shaderfx_ready(void)
{
	return fx.active;
}

/* ==================================================================== */
/* frame lifecycle                                                      */
/* ==================================================================== */

void gpu_shaderfx_frame_begin(void)
{
	if (!fx.active) {
		return;
	}

	fx.ring = (fx.ring + 1) % FX_RING;
	fx.count = 0;
	fx.range_start = 0;
	fx.run_texture = NULL;
	fx.stat_draws = 0;
	fx.stat_sprites = 0;
	fx.stat_flush_tex = 0;
	fx.stat_flush_direct = 0;

	/* cycle=true: never stall even if this slot's previous use is
	 * somehow still in flight */
	fx.mapped = SDL_MapGPUTransferBuffer(sdlgpu, fx.inst_transfer[fx.ring], true);
	if (!fx.mapped) {
		note("gpu_shaderfx: transfer map failed, batching off this frame: %s", SDL_GetError());
		fx.in_frame = false;
		return;
	}
	fx.in_frame = true;
}

void gpu_shaderfx_flush(void)
{
	if (!fx.in_frame) {
		return;
	}

	int n = fx.count - fx.range_start;
	if (n <= 0 || !fx.run_texture) {
		fx.run_texture = NULL;
		fx.range_start = fx.count;
		return;
	}

	SDL_GPURenderPass *pass = gpu_get_render_pass();
	SDL_GPUCommandBuffer *cmd = gpu_get_command_buffer();
	if (!pass || !cmd) {
		/* no pass to record into - drop the run */
		fx.run_texture = NULL;
		fx.range_start = fx.count;
		return;
	}

	SDL_BindGPUGraphicsPipeline(pass, fx.pipeline);

	SDL_GPUBufferBinding vb = {.buffer = fx.quad_vbo, .offset = 0};
	SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
	SDL_BindGPUVertexStorageBuffers(pass, 0, &fx.inst_buf[fx.ring], 1);

	SDL_GPUTextureSamplerBinding tsb = {.texture = fx.run_texture, .sampler = fx.sampler};
	SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
	SDL_BindGPUFragmentStorageBuffers(pass, 0, &fx.inst_buf[fx.ring], 1);

	int sw, sh;
	gpu_get_swapchain_size(&sw, &sh);
	gpu_fx_vs_uniforms_t vsu = {
	    .screen_w = (float)sw,
	    .screen_h = (float)sh,
	    .inv_screen_w = (sw > 0) ? 1.0f / (float)sw : 0.0f,
	    .inv_screen_h = (sh > 0) ? 1.0f / (float)sh : 0.0f,
	    .base_instance = (uint32_t)fx.range_start,
	};
	SDL_PushGPUVertexUniformData(cmd, 0, &vsu, sizeof(vsu));

	int le_bonus = ((game_options & GO_LIGHTER) ? 8 : 0) + ((game_options & GO_LIGHTER2) ? 12 : 0);
	gpu_fx_ps_uniforms_t psu = {
	    .sdl_scale = sdl_scale, .le_bonus = le_bonus, .base_instance = (uint32_t)fx.range_start};
	SDL_PushGPUFragmentUniformData(cmd, 0, &psu, sizeof(psu));

	SDL_DrawGPUPrimitives(pass, 6, (Uint32)n, 0, 0);
	gpu_debug_increment_draw_count();

	fx.stat_draws++;
	fx.stat_sprites += n;

	fx.run_texture = NULL;
	fx.range_start = fx.count;
}

void gpu_shaderfx_submit_upload(void)
{
	if (!fx.active) {
		return;
	}
	if (fx.mapped) {
		SDL_UnmapGPUTransferBuffer(sdlgpu, fx.inst_transfer[fx.ring]);
		fx.mapped = NULL;
	}
	bool had_frame = fx.in_frame;
	fx.in_frame = false;

	if (!had_frame || fx.count == 0) {
		return;
	}

	/* Submitted BEFORE sdl_gpu.c submits the main render command buffer;
	 * SDL_GPU executes command buffers in submission order and tracks
	 * the copy-then-read hazard itself, so no fence is needed. */
	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
	if (!cmd) {
		note("gpu_shaderfx: upload cmd acquire failed: %s", SDL_GetError());
		return;
	}
	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	if (!cp) {
		SDL_CancelGPUCommandBuffer(cmd);
		return;
	}
	SDL_GPUTransferBufferLocation src = {.transfer_buffer = fx.inst_transfer[fx.ring], .offset = 0};
	SDL_GPUBufferRegion dst = {
	    .buffer = fx.inst_buf[fx.ring], .offset = 0, .size = (Uint32)((size_t)fx.count * sizeof(gpu_fx_instance_t))};
	SDL_UploadToGPUBuffer(cp, &src, &dst, false);
	SDL_EndGPUCopyPass(cp);
	SDL_SubmitGPUCommandBuffer(cmd);
}

/* ==================================================================== */
/* draw submission                                                      */
/* ==================================================================== */

static bool fx_batch_add(SDL_GPUTexture *texture, const gpu_fx_instance_t *inst)
{
	if (!fx.in_frame || !fx.mapped) {
		return false;
	}
	if (fx.count >= FX_MAX_INSTANCES) {
		return false;
	}
	if (fx.run_texture && texture != fx.run_texture) {
		fx.stat_flush_tex++;
		gpu_shaderfx_flush();
	}
	fx.run_texture = texture;
	fx.mapped[fx.count] = *inst;
	fx.count++;
	return true;
}

int sdl_blit_fx(int cache_index, const gpu_fx_draw_t *d, int sx, int sy, int clipsx, int clipsy, int clipex, int clipey,
    int x_offset, int y_offset)
{
	struct sdl_texture *st;
	int addx = 0, addy = 0;

	if (!fx.active || !fx.in_frame || cache_index < 0) {
		return 0;
	}

	st = &sdlt[cache_index];
	if (!st->gpu_tex) {
		return 0; /* base texture not uploaded (yet) - let the caller fall back */
	}

	int pix_w = st->xres * sdl_scale;
	int pix_h = st->yres * sdl_scale;
	int dx = st->xres;
	int dy = st->yres;

	/* same clip math as sdl_blit_gpu_tex (logical coordinates) */
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
		return 1; /* completely clipped - nothing to draw, but handled */
	}

	/* replicate sdl_make's sink clamp against the sprite height */
	int sink = 0;
	if (d->sink) {
		int lim = st->yres - 4;
		if (lim < 0) {
			lim = 0;
		}
		sink = (d->sink < lim) ? d->sink : lim;
	}

	/* atlas entries live at an offset inside a shared page texture */
	int ax = st->atlas_used ? st->atlas_x : 0;
	int ay = st->atlas_used ? st->atlas_y : 0;

	gpu_fx_instance_t inst = {
	    .dest = {(float)((sx + x_offset) * sdl_scale), (float)((sy + y_offset) * sdl_scale), (float)(dx * sdl_scale),
	        (float)(dy * sdl_scale)},
	    .src = {ax + addx * sdl_scale, ay + addy * sdl_scale, dx * sdl_scale, dy * sdl_scale},
	    .org_sz = {ax, ay, pix_w, pix_h},
	    .colorize = {(uint32_t)d->c1, (uint32_t)d->c2, (uint32_t)d->c3,
	        (d->sprite >= 220000) ? GPU_FX_COLORIZE_NEW : 0u},
	    .balance = {d->cr, d->cg, d->cb, d->light},
	    .fx = {d->sat, d->shine, d->freeze, sink * sdl_scale},
	    .light_a = {d->ml, d->ll, d->rl, d->ul},
	    .light_b = {d->dl, d->alpha ? d->alpha : 255, 0, 0},
	};

	return fx_batch_add(st->gpu_tex, &inst) ? 1 : 0;
}

int gpu_shaderfx_capacity(void)
{
	if (!fx.in_frame || !fx.mapped) {
		return 0;
	}
	return FX_MAX_INSTANCES - fx.count;
}

int gpu_shaderfx_plain_quad(SDL_GPUTexture *tex, float dest_x, float dest_y, float dest_w, float dest_h, int src_x,
    int src_y, int src_w, int src_h, int r, int g, int b, int alpha)
{
	if (!fx.active || !fx.in_frame || !tex) {
		return 0;
	}
	if (dest_w <= 0.0f || dest_h <= 0.0f || src_w <= 0 || src_h <= 0) {
		return 1; /* nothing visible - handled */
	}

	gpu_fx_instance_t inst = {
	    .dest = {dest_x, dest_y, dest_w, dest_h},
	    .src = {src_x, src_y, src_w, src_h},
	    /* org_sz is only read by the effect pipeline; keep it covering the
	     * source region so nothing indexes outside the page */
	    .org_sz = {src_x, src_y, src_w, src_h},
	    .colorize = {0, 0, 0, GPU_FX_MODE_PLAIN},
	    .balance = {r, g, b, 0},
	    .fx = {0, 0, 0, 0},
	    .light_a = {15, 15, 15, 15},
	    .light_b = {15, alpha, 0, 0},
	};

	return fx_batch_add(tex, &inst) ? 1 : 0;
}

/* Called by the direct-draw path so interleaved non-batched draws keep
 * their painter order relative to batched sprites. */
void gpu_shaderfx_direct_draw_barrier(void)
{
	if (fx.in_frame && fx.count > fx.range_start) {
		fx.stat_flush_direct++;
		gpu_shaderfx_flush();
	}
}

void gpu_shaderfx_get_stats(int *draws, int *sprites, int *tex_flushes, int *direct_flushes)
{
	if (draws) {
		*draws = fx.stat_draws;
	}
	if (sprites) {
		*sprites = fx.stat_sprites;
	}
	if (tex_flushes) {
		*tex_flushes = fx.stat_flush_tex;
	}
	if (direct_flushes) {
		*direct_flushes = fx.stat_flush_direct;
	}
}
