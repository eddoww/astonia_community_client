/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Glow - batched additive capsule glows for spell effects
 *
 * See sdl_gpu_glow.h for what this replaces and why. The batching is a
 * direct copy of the sdl_gpu_shaderfx.c model (instance ring, mapped
 * transfer buffer written as draws are recorded, one upload command
 * buffer submitted before the render one, base-instance uniform); the
 * only structural difference is that glows carry no texture, so runs are
 * only ever broken by capacity or by a painter-order barrier.
 */

#include <stdio.h>
#include <string.h>

#include "astonia.h"
#include "sdl/sdl.h"
#include "sdl/sdl_private.h"
#include "sdl/sdl_gpu.h"
#include "sdl/sdl_gpu_glow.h"
#include "sdl/sdl_gpu_shaderfx.h"
#include "sdl/sdl_gpu_prim.h"

_Static_assert(sizeof(gpu_glow_instance_t) == 48, "gpu_glow_instance_t must be 48 bytes (matches std430 struct)");

#define GLOW_MAX_INSTANCES 16384
#define GLOW_RING          3

typedef struct glow_vertex {
	float x, y;
} glow_vertex_t;

static const glow_vertex_t quad_vertices[6] = {
    {0.0f, 0.0f},
    {1.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 1.0f},
};

static struct {
	SDL_GPUGraphicsPipeline *pipeline;
	SDL_GPUBuffer *quad_vbo;

	SDL_GPUBuffer *inst_buf[GLOW_RING];
	SDL_GPUTransferBuffer *inst_transfer[GLOW_RING];
	int ring;

	gpu_glow_instance_t *mapped;
	int count;
	int range_start;

	bool active;
	bool in_frame;

	int stat_draws;
	int stat_glows;
} gl = {0};

/* ==================================================================== */
/* setup                                                                */
/* ==================================================================== */

static SDL_GPUShader *glow_load_shader(
    const char *path, SDL_GPUShaderFormat fmt, SDL_GPUShaderStage stage, Uint32 num_storage, Uint32 num_uniform)
{
	size_t size;
	void *code = SDL_LoadFile(path, &size);
	if (!code) {
		note("gpu_glow: cannot load %s", path);
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
		note("gpu_glow: shader create failed for %s: %s", path, SDL_GetError());
	}
	return sh;
}

static bool glow_create_quad_vbo(void)
{
	SDL_GPUBufferCreateInfo info = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = sizeof(quad_vertices)};
	gl.quad_vbo = SDL_CreateGPUBuffer(sdlgpu, &info);
	if (!gl.quad_vbo) {
		note("gpu_glow: quad vbo create failed: %s", SDL_GetError());
		return false;
	}

	SDL_GPUTransferBufferCreateInfo ti = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = sizeof(quad_vertices)};
	SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(sdlgpu, &ti);
	if (!tb) {
		note("gpu_glow: quad transfer create failed: %s", SDL_GetError());
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
	SDL_GPUBufferRegion region = {.buffer = gl.quad_vbo, .offset = 0, .size = sizeof(quad_vertices)};
	SDL_UploadToGPUBuffer(cp, &src, &region, false);
	SDL_EndGPUCopyPass(cp);
	SDL_SubmitGPUCommandBuffer(cmd);
	SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
	return true;
}

static bool glow_create_pipeline(void)
{
	SDL_GPUShaderFormat fmt = gpu_preferred_shader_format();

	if (!fmt) {
		note("gpu_glow: no supported shader format on this backend");
		return false;
	}

	/* same clean fallback as the shader-effects path: when the compiled
	 * shader for this backend's format is missing, glow self-disables and
	 * the effects stay on their hand-rolled falloffs */
	char vs_path[256], ps_path[256];
	snprintf(vs_path, sizeof(vs_path), "res/shaders/compiled/glow_vs.%s", gpu_shader_file_ext(fmt));
	snprintf(ps_path, sizeof(ps_path), "res/shaders/compiled/glow_ps.%s", gpu_shader_file_ext(fmt));

	SDL_GPUShader *vs = glow_load_shader(vs_path, fmt, SDL_GPU_SHADERSTAGE_VERTEX, 1, 1);
	SDL_GPUShader *ps = glow_load_shader(ps_path, fmt, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
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
	    .slot = 0, .pitch = sizeof(glow_vertex_t), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX};
	SDL_GPUVertexAttribute attr = {
	    .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0};

	/* classic particle additive: dst += src.rgb * src.a. Mirrors mode 1
	 * (ADD) of gpu_blend_state() in sdl_gpu_draw.c. */
	SDL_GPUColorTargetDescription color_desc = {
	    .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
	    .blend_state = {.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
	        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
	        .color_blend_op = SDL_GPU_BLENDOP_ADD,
	        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
	        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
	        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
	        .enable_blend = true},
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

	gl.pipeline = SDL_CreateGPUGraphicsPipeline(sdlgpu, &info);
	SDL_ReleaseGPUShader(sdlgpu, vs);
	SDL_ReleaseGPUShader(sdlgpu, ps);
	if (!gl.pipeline) {
		note("gpu_glow: pipeline create failed: %s", SDL_GetError());
		return false;
	}
	return true;
}

bool gpu_glow_init(void)
{
	if (!use_gpu_rendering || !sdlgpu) {
		return false;
	}
	if (gl.active) {
		return true;
	}

	memset(&gl, 0, sizeof(gl));

	if (!glow_create_pipeline()) {
		gpu_glow_shutdown();
		return false;
	}
	if (!glow_create_quad_vbo()) {
		gpu_glow_shutdown();
		return false;
	}

	for (int i = 0; i < GLOW_RING; i++) {
		SDL_GPUBufferCreateInfo bi = {.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
		    .size = GLOW_MAX_INSTANCES * sizeof(gpu_glow_instance_t)};
		gl.inst_buf[i] = SDL_CreateGPUBuffer(sdlgpu, &bi);

		SDL_GPUTransferBufferCreateInfo ti = {
		    .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = GLOW_MAX_INSTANCES * sizeof(gpu_glow_instance_t)};
		gl.inst_transfer[i] = SDL_CreateGPUTransferBuffer(sdlgpu, &ti);

		if (!gl.inst_buf[i] || !gl.inst_transfer[i]) {
			note("gpu_glow: instance ring alloc failed: %s", SDL_GetError());
			gpu_glow_shutdown();
			return false;
		}
	}

	gl.active = true;
	return true;
}

void gpu_glow_shutdown(void)
{
	if (!sdlgpu) {
		memset(&gl, 0, sizeof(gl));
		return;
	}
	if (gl.mapped) {
		SDL_UnmapGPUTransferBuffer(sdlgpu, gl.inst_transfer[gl.ring]);
		gl.mapped = NULL;
	}
	if (gl.pipeline) {
		SDL_ReleaseGPUGraphicsPipeline(sdlgpu, gl.pipeline);
	}
	if (gl.quad_vbo) {
		SDL_ReleaseGPUBuffer(sdlgpu, gl.quad_vbo);
	}
	for (int i = 0; i < GLOW_RING; i++) {
		if (gl.inst_buf[i]) {
			SDL_ReleaseGPUBuffer(sdlgpu, gl.inst_buf[i]);
		}
		if (gl.inst_transfer[i]) {
			SDL_ReleaseGPUTransferBuffer(sdlgpu, gl.inst_transfer[i]);
		}
	}
	memset(&gl, 0, sizeof(gl));
}

bool gpu_glow_ready(void)
{
	return gl.active;
}

/* ==================================================================== */
/* frame lifecycle                                                      */
/* ==================================================================== */

void gpu_glow_frame_begin(void)
{
	if (!gl.active) {
		return;
	}

	gl.ring = (gl.ring + 1) % GLOW_RING;
	gl.count = 0;
	gl.range_start = 0;
	gl.stat_draws = 0;
	gl.stat_glows = 0;

	gl.mapped = SDL_MapGPUTransferBuffer(sdlgpu, gl.inst_transfer[gl.ring], true);
	if (!gl.mapped) {
		note("gpu_glow: transfer map failed, glows off this frame: %s", SDL_GetError());
		gl.in_frame = false;
		return;
	}
	gl.in_frame = true;
}

void gpu_glow_flush(void)
{
	if (!gl.in_frame) {
		return;
	}

	int n = gl.count - gl.range_start;
	if (n <= 0) {
		gl.range_start = gl.count;
		return;
	}

	SDL_GPURenderPass *pass = gpu_get_render_pass();
	SDL_GPUCommandBuffer *cmd = gpu_get_command_buffer();
	if (!pass || !cmd) {
		gl.range_start = gl.count;
		return;
	}

	SDL_BindGPUGraphicsPipeline(pass, gl.pipeline);

	SDL_GPUBufferBinding vb = {.buffer = gl.quad_vbo, .offset = 0};
	SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
	SDL_BindGPUVertexStorageBuffers(pass, 0, &gl.inst_buf[gl.ring], 1);

	int sw, sh;
	gpu_get_swapchain_size(&sw, &sh);
	gpu_glow_vs_uniforms_t vsu = {
	    .inv_screen_w = (sw > 0) ? 1.0f / (float)sw : 0.0f,
	    .inv_screen_h = (sh > 0) ? 1.0f / (float)sh : 0.0f,
	    .base_instance = (uint32_t)gl.range_start,
	};
	SDL_PushGPUVertexUniformData(cmd, 0, &vsu, sizeof(vsu));

	SDL_DrawGPUPrimitives(pass, 6, (Uint32)n, 0, 0);
	gpu_debug_increment_draw_count();

	gl.stat_draws++;
	gl.stat_glows += n;
	gl.range_start = gl.count;
}

void gpu_glow_submit_upload(void)
{
	if (!gl.active) {
		return;
	}
	if (gl.mapped) {
		SDL_UnmapGPUTransferBuffer(sdlgpu, gl.inst_transfer[gl.ring]);
		gl.mapped = NULL;
	}
	bool had_frame = gl.in_frame;
	gl.in_frame = false;

	if (!had_frame || gl.count == 0) {
		return;
	}

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
	if (!cmd) {
		note("gpu_glow: upload cmd acquire failed: %s", SDL_GetError());
		return;
	}
	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	if (!cp) {
		SDL_CancelGPUCommandBuffer(cmd);
		return;
	}
	SDL_GPUTransferBufferLocation src = {.transfer_buffer = gl.inst_transfer[gl.ring], .offset = 0};
	SDL_GPUBufferRegion dst = {
	    .buffer = gl.inst_buf[gl.ring], .offset = 0, .size = (Uint32)((size_t)gl.count * sizeof(gpu_glow_instance_t))};
	SDL_UploadToGPUBuffer(cp, &src, &dst, false);
	SDL_EndGPUCopyPass(cp);
	SDL_SubmitGPUCommandBuffer(cmd);
}

void gpu_glow_direct_draw_barrier(void)
{
	if (gl.in_frame && gl.count > gl.range_start) {
		gpu_glow_flush();
	}
}

void gpu_glow_get_stats(int *draws, int *glows)
{
	if (draws) {
		*draws = gl.stat_draws;
	}
	if (glows) {
		*glows = gl.stat_glows;
	}
}

/* ==================================================================== */
/* draw submission                                                      */
/* ==================================================================== */

bool gpu_glow_add(
    float x0, float y0, float x1, float y1, float radius, float core, float r, float g, float b, float intensity)
{
	if (!gl.in_frame || !gl.mapped) {
		return false;
	}
	if (gl.count >= GLOW_MAX_INSTANCES) {
		return false;
	}

	/* anything another batch recorded before this glow must land
	 * underneath it */
	gpu_shaderfx_direct_draw_barrier();
	gpu_prim_batch_direct_draw_barrier();

	gpu_glow_instance_t *inst = &gl.mapped[gl.count];
	inst->seg[0] = x0;
	inst->seg[1] = y0;
	inst->seg[2] = x1;
	inst->seg[3] = y1;
	inst->shape[0] = radius;
	inst->shape[1] = core;
	inst->shape[2] = 0.0f;
	inst->shape[3] = 0.0f;
	inst->color[0] = r;
	inst->color[1] = g;
	inst->color[2] = b;
	inst->color[3] = intensity;
	gl.count++;
	return true;
}
