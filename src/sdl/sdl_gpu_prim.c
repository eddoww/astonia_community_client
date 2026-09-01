/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Primitive Batch
 *
 * See sdl_gpu_prim.h for what this replaces and why. The batching is the
 * same model as sdl_gpu_shaderfx.c / sdl_gpu_glow.c (instance ring,
 * mapped transfer buffer written as draws are recorded, one upload
 * command buffer submitted before the render one, base-instance
 * uniform). Rects carry no texture, so a run is only ever broken by
 * capacity, by a blend-mode change, or by a painter-order barrier.
 */

#include <stdio.h>
#include <string.h>

#include "astonia.h"
#include "sdl/sdl.h"
#include "sdl/sdl_private.h"
#include "sdl/sdl_gpu.h"
#include "sdl/sdl_gpu_prim.h"
#include "sdl/sdl_gpu_shaderfx.h"
#include "sdl/sdl_gpu_glow.h"
#include "sdl/sdl_gpu_draw.h"

_Static_assert(sizeof(gpu_prim_instance_t) == 32, "gpu_prim_instance_t must be 32 bytes (matches std430 struct)");

#define PRIM_MAX_INSTANCES 16384
#define PRIM_RING          3

typedef struct prim_vertex {
	float x, y;
} prim_vertex_t;

static const prim_vertex_t quad_vertices[6] = {
    {0.0f, 0.0f},
    {1.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 1.0f},
};

static struct {
	SDL_GPUGraphicsPipeline *pipelines[GPU_DRAW_BLEND_MODES];
	SDL_GPUBuffer *quad_vbo;
	int run_blend; /* blend mode the pending run was opened with */

	SDL_GPUBuffer *inst_buf[PRIM_RING];
	SDL_GPUTransferBuffer *inst_transfer[PRIM_RING];
	int ring;

	gpu_prim_instance_t *mapped;
	int count;
	int range_start;

	bool active;
	bool in_frame;

	int stat_draws;
	int stat_rects;
} pb = {0};

/* ==================================================================== */
/* setup                                                                */
/* ==================================================================== */

static SDL_GPUShader *prim_load_shader(
    const char *path, SDL_GPUShaderFormat fmt, SDL_GPUShaderStage stage, Uint32 num_storage, Uint32 num_uniform)
{
	size_t size;
	void *code = SDL_LoadFile(path, &size);
	if (!code) {
		note("gpu_prim: cannot load %s", path);
		return NULL;
	}

	SDL_GPUShaderCreateInfo info = {
	    .code = code,
	    .code_size = size,
	    .entrypoint = gpu_shader_entrypoint(fmt),
	    .format = fmt,
	    .stage = stage,
	    .num_samplers = 0,
	    .num_storage_textures = 0,
	    .num_storage_buffers = num_storage,
	    .num_uniform_buffers = num_uniform,
	};
	SDL_GPUShader *sh = SDL_CreateGPUShader(sdlgpu, &info);
	SDL_free(code);
	if (!sh) {
		note("gpu_prim: shader create failed for %s: %s", path, SDL_GetError());
	}
	return sh;
}

static bool prim_create_quad_vbo(void)
{
	SDL_GPUBufferCreateInfo info = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = sizeof(quad_vertices)};
	pb.quad_vbo = SDL_CreateGPUBuffer(sdlgpu, &info);
	if (!pb.quad_vbo) {
		note("gpu_prim: quad vbo create failed: %s", SDL_GetError());
		return false;
	}

	SDL_GPUTransferBufferCreateInfo ti = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = sizeof(quad_vertices)};
	SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(sdlgpu, &ti);
	if (!tb) {
		note("gpu_prim: quad transfer create failed: %s", SDL_GetError());
		return false;
	}

	void *dst = SDL_MapGPUTransferBuffer(sdlgpu, tb, false);
	if (!dst) {
		SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
		return false;
	}
	memcpy(dst, quad_vertices, sizeof(quad_vertices));
	SDL_UnmapGPUTransferBuffer(sdlgpu, tb);

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
	if (!cmd) {
		SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
		return false;
	}
	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	if (!cp) {
		SDL_CancelGPUCommandBuffer(cmd);
		SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
		return false;
	}
	SDL_GPUTransferBufferLocation src = {.transfer_buffer = tb, .offset = 0};
	SDL_GPUBufferRegion region = {.buffer = pb.quad_vbo, .offset = 0, .size = sizeof(quad_vertices)};
	SDL_UploadToGPUBuffer(cp, &src, &region, false);
	SDL_EndGPUCopyPass(cp);
	SDL_SubmitGPUCommandBuffer(cmd);
	SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
	return true;
}

static bool prim_create_pipeline(void)
{
	SDL_GPUShaderFormat fmt = gpu_preferred_shader_format();

	if (!fmt) {
		note("gpu_prim: no supported shader format on this backend");
		return false;
	}

	/* same clean fallback as the shader-effects path: when the compiled
	 * shader for this backend's format is missing, the batch stays off
	 * and gpu_draw_rect() keeps its one-draw-per-rect path */
	char vs_path[256], ps_path[256];
	snprintf(vs_path, sizeof(vs_path), "res/shaders/compiled/prim_batch_vs.%s", gpu_shader_file_ext(fmt));
	snprintf(ps_path, sizeof(ps_path), "res/shaders/compiled/prim_batch_ps.%s", gpu_shader_file_ext(fmt));

	SDL_GPUShader *vs = prim_load_shader(vs_path, fmt, SDL_GPU_SHADERSTAGE_VERTEX, 1, 1);
	SDL_GPUShader *ps = prim_load_shader(ps_path, fmt, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
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
	    .slot = 0, .pitch = sizeof(prim_vertex_t), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX};
	SDL_GPUVertexAttribute attr = {
	    .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0};

	/* one pipeline per blend mode, exactly like the unbatched primitive
	 * pipelines - the mode is baked in, so a run ends when it changes */
	bool ok = true;
	for (int mode = 0; mode < GPU_DRAW_BLEND_MODES; mode++) {
		SDL_GPUColorTargetDescription color_desc = {
		    .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
		    .blend_state = gpu_draw_blend_state(mode),
		};

		SDL_GPUGraphicsPipelineCreateInfo info = {
		    .vertex_shader = vs,
		    .fragment_shader = ps,
		    .vertex_input_state = {.vertex_buffer_descriptions = &vb_desc,
		        .num_vertex_buffers = 1,
		        .vertex_attributes = &attr,
		        .num_vertex_attributes = 1},
		    .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		    .rasterizer_state = {.fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE},
		    .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1, .sample_mask = 0xFFFFFFFF},
		    .target_info = {.color_target_descriptions = &color_desc, .num_color_targets = 1},
		};

		pb.pipelines[mode] = SDL_CreateGPUGraphicsPipeline(sdlgpu, &info);
		if (!pb.pipelines[mode]) {
			note("gpu_prim: pipeline (blend %d) create failed: %s", mode, SDL_GetError());
			ok = false;
		}
	}
	SDL_ReleaseGPUShader(sdlgpu, vs);
	SDL_ReleaseGPUShader(sdlgpu, ps);
	return ok;
}

bool gpu_prim_batch_init(void)
{
	if (!use_gpu_rendering || !sdlgpu) {
		return false;
	}

	/* Escape hatch. Batching changes the ORDER draws reach the GPU (a run
	 * is emitted when something else needs to draw over it), so if a
	 * layering bug ever shows up in the wild this isolates it in one
	 * restart instead of one build. */
	{
		const char *env = SDL_getenv("ASTONIA_GPU_NO_PRIM_BATCH");
		if (env && *env && *env != '0') {
			note("gpu_prim: batching disabled by ASTONIA_GPU_NO_PRIM_BATCH");
			return false;
		}
	}
	if (pb.active) {
		return true;
	}

	memset(&pb, 0, sizeof(pb));

	if (!prim_create_pipeline()) {
		gpu_prim_batch_shutdown();
		return false;
	}
	if (!prim_create_quad_vbo()) {
		gpu_prim_batch_shutdown();
		return false;
	}

	for (int i = 0; i < PRIM_RING; i++) {
		SDL_GPUBufferCreateInfo bi = {.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
		    .size = PRIM_MAX_INSTANCES * sizeof(gpu_prim_instance_t)};
		pb.inst_buf[i] = SDL_CreateGPUBuffer(sdlgpu, &bi);

		SDL_GPUTransferBufferCreateInfo ti = {
		    .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = PRIM_MAX_INSTANCES * sizeof(gpu_prim_instance_t)};
		pb.inst_transfer[i] = SDL_CreateGPUTransferBuffer(sdlgpu, &ti);

		if (!pb.inst_buf[i] || !pb.inst_transfer[i]) {
			note("gpu_prim: instance ring alloc failed: %s", SDL_GetError());
			gpu_prim_batch_shutdown();
			return false;
		}
	}

	pb.active = true;
	return true;
}

void gpu_prim_batch_shutdown(void)
{
	if (!sdlgpu) {
		memset(&pb, 0, sizeof(pb));
		return;
	}
	if (pb.mapped) {
		SDL_UnmapGPUTransferBuffer(sdlgpu, pb.inst_transfer[pb.ring]);
		pb.mapped = NULL;
	}
	for (int i = 0; i < GPU_DRAW_BLEND_MODES; i++) {
		if (pb.pipelines[i]) {
			SDL_ReleaseGPUGraphicsPipeline(sdlgpu, pb.pipelines[i]);
		}
	}
	if (pb.quad_vbo) {
		SDL_ReleaseGPUBuffer(sdlgpu, pb.quad_vbo);
	}
	for (int i = 0; i < PRIM_RING; i++) {
		if (pb.inst_buf[i]) {
			SDL_ReleaseGPUBuffer(sdlgpu, pb.inst_buf[i]);
		}
		if (pb.inst_transfer[i]) {
			SDL_ReleaseGPUTransferBuffer(sdlgpu, pb.inst_transfer[i]);
		}
	}
	memset(&pb, 0, sizeof(pb));
}

bool gpu_prim_batch_ready(void)
{
	return pb.active;
}

/* ==================================================================== */
/* frame lifecycle                                                      */
/* ==================================================================== */

void gpu_prim_batch_frame_begin(void)
{
	if (!pb.active) {
		return;
	}

	pb.ring = (pb.ring + 1) % PRIM_RING;
	pb.count = 0;
	pb.range_start = 0;
	pb.stat_draws = 0;
	pb.stat_rects = 0;

	pb.mapped = SDL_MapGPUTransferBuffer(sdlgpu, pb.inst_transfer[pb.ring], true);
	if (!pb.mapped) {
		note("gpu_prim: transfer map failed, rects unbatched this frame: %s", SDL_GetError());
		pb.in_frame = false;
		return;
	}
	pb.in_frame = true;
}

void gpu_prim_batch_flush(void)
{
	if (!pb.in_frame) {
		return;
	}

	int n = pb.count - pb.range_start;
	if (n <= 0) {
		pb.range_start = pb.count;
		return;
	}

	SDL_GPURenderPass *pass = gpu_get_render_pass();
	SDL_GPUCommandBuffer *cmd = gpu_get_command_buffer();
	if (!pass || !cmd) {
		pb.range_start = pb.count;
		return;
	}

	int mode = pb.run_blend;
	if (mode < 0 || mode >= GPU_DRAW_BLEND_MODES || !pb.pipelines[mode]) {
		mode = 0;
	}
	SDL_BindGPUGraphicsPipeline(pass, pb.pipelines[mode]);

	SDL_GPUBufferBinding vb = {.buffer = pb.quad_vbo, .offset = 0};
	SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
	SDL_BindGPUVertexStorageBuffers(pass, 0, &pb.inst_buf[pb.ring], 1);

	int sw, sh;
	gpu_get_swapchain_size(&sw, &sh);
	gpu_prim_vs_uniforms_t vsu = {
	    .inv_screen_w = (sw > 0) ? 1.0f / (float)sw : 0.0f,
	    .inv_screen_h = (sh > 0) ? 1.0f / (float)sh : 0.0f,
	    .base_instance = (uint32_t)pb.range_start,
	};
	SDL_PushGPUVertexUniformData(cmd, 0, &vsu, sizeof(vsu));

	SDL_DrawGPUPrimitives(pass, 6, (Uint32)n, 0, 0);
	gpu_debug_increment_draw_count();

	pb.stat_draws++;
	pb.stat_rects += n;
	pb.range_start = pb.count;
}

void gpu_prim_batch_submit_upload(void)
{
	if (!pb.active) {
		return;
	}
	if (pb.mapped) {
		SDL_UnmapGPUTransferBuffer(sdlgpu, pb.inst_transfer[pb.ring]);
		pb.mapped = NULL;
	}
	bool had_frame = pb.in_frame;
	pb.in_frame = false;

	if (!had_frame || pb.count == 0) {
		return;
	}

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
	if (!cmd) {
		note("gpu_prim: upload cmd acquire failed: %s", SDL_GetError());
		return;
	}
	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	if (!cp) {
		SDL_CancelGPUCommandBuffer(cmd);
		return;
	}
	SDL_GPUTransferBufferLocation src = {.transfer_buffer = pb.inst_transfer[pb.ring], .offset = 0};
	SDL_GPUBufferRegion dst = {
	    .buffer = pb.inst_buf[pb.ring], .offset = 0, .size = (Uint32)((size_t)pb.count * sizeof(gpu_prim_instance_t))};
	SDL_UploadToGPUBuffer(cp, &src, &dst, false);
	SDL_EndGPUCopyPass(cp);
	SDL_SubmitGPUCommandBuffer(cmd);
}

void gpu_prim_batch_direct_draw_barrier(void)
{
	if (pb.in_frame && pb.count > pb.range_start) {
		gpu_prim_batch_flush();
	}
}

void gpu_prim_batch_get_stats(int *draws, int *rects)
{
	if (draws) {
		*draws = pb.stat_draws;
	}
	if (rects) {
		*rects = pb.stat_rects;
	}
}

/* ==================================================================== */
/* draw submission                                                      */
/* ==================================================================== */

bool gpu_prim_batch_add(float x, float y, float w, float h, float r, float g, float b, float a)
{
	if (!pb.in_frame || !pb.mapped) {
		return false;
	}
	if (pb.count >= PRIM_MAX_INSTANCES) {
		return false;
	}

	/* the blend mode is baked into the pipeline, so a change ends the run */
	int mode = gpu_draw_get_blend_mode();
	if (pb.count > pb.range_start && mode != pb.run_blend) {
		gpu_prim_batch_flush();
	}
	pb.run_blend = mode;

	/* anything recorded by another batch before this rect must land
	 * underneath it */
	gpu_shaderfx_direct_draw_barrier();
	gpu_glow_direct_draw_barrier();

	gpu_prim_instance_t *inst = &pb.mapped[pb.count];
	inst->dest[0] = x;
	inst->dest[1] = y;
	inst->dest[2] = w;
	inst->dest[3] = h;
	inst->color[0] = r;
	inst->color[1] = g;
	inst->color[2] = b;
	inst->color[3] = a;
	pb.count++;
	return true;
}
