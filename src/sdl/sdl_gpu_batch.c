/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Sprite Batching Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "astonia.h"
#include "sdl/sdl_gpu_batch.h"

// Verify struct size matches shader expectation (128 bytes)
_Static_assert(sizeof(gpu_sprite_instance_t) == 128,
               "gpu_sprite_instance_t must be 128 bytes to match GLSL struct");
#include "sdl/sdl_gpu.h"
#include "helper/helper.h"

// Global batch state
static gpu_batch_state_t batch_state = {0};

// Current render pass (set during frame)
static SDL_GPURenderPass *current_pass = NULL;
static SDL_GPUCommandBuffer *current_cmd = NULL;

// Quad vertices (single quad, instanced rendering)
typedef struct batch_vertex {
    float x, y;     // Position (0-1 for quad corners)
    float u, v;     // Texture coordinates (0-1)
} batch_vertex_t;

static const batch_vertex_t quad_vertices[6] = {
    // Triangle 1
    {0.0f, 0.0f, 0.0f, 0.0f},  // Top-left
    {1.0f, 0.0f, 1.0f, 0.0f},  // Top-right
    {1.0f, 1.0f, 1.0f, 1.0f},  // Bottom-right
    // Triangle 2
    {0.0f, 0.0f, 0.0f, 0.0f},  // Top-left
    {1.0f, 1.0f, 1.0f, 1.0f},  // Bottom-right
    {0.0f, 1.0f, 0.0f, 1.0f},  // Bottom-left
};

// ============================================================================
// Shader Loading
// ============================================================================

static SDL_GPUShaderFormat get_shader_format(void) {
    if (!sdlgpu) return 0;

    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(sdlgpu);

    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        return SDL_GPU_SHADERFORMAT_SPIRV;
    }
    if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        return SDL_GPU_SHADERFORMAT_DXIL;
    }
    if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        return SDL_GPU_SHADERFORMAT_MSL;
    }
    return 0;
}

static SDL_GPUShader *load_shader(const char *filename, SDL_GPUShaderStage stage,
                                   Uint32 num_samplers, Uint32 num_storage_buffers,
                                   Uint32 num_uniform_buffers) {
    if (!sdlgpu) return NULL;

    FILE *f = fopen(filename, "rb");
    if (!f) {
        note("gpu_batch load_shader: Cannot open %s", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = (char *)malloc(size);
    if (!data) {
        fclose(f);
        note("gpu_batch load_shader: Out of memory for %s", filename);
        return NULL;
    }

    if (fread(data, 1, size, f) != size) {
        free(data);
        fclose(f);
        note("gpu_batch load_shader: Failed to read %s", filename);
        return NULL;
    }
    fclose(f);

    SDL_GPUShaderFormat fmt = get_shader_format();
    const char *entrypoint;
    if (fmt == SDL_GPU_SHADERFORMAT_SPIRV) {
        entrypoint = "main";  // GLSL uses "main"
    } else {
        entrypoint = (stage == SDL_GPU_SHADERSTAGE_VERTEX) ? "VSMain" : "PSMain";
    }

    SDL_GPUShaderCreateInfo info = {
        .code = (const Uint8 *)data,
        .code_size = size,
        .entrypoint = entrypoint,
        .format = fmt,
        .stage = stage,
        .num_samplers = num_samplers,
        .num_storage_textures = 0,
        .num_storage_buffers = num_storage_buffers,
        .num_uniform_buffers = num_uniform_buffers
    };

    SDL_GPUShader *shader = SDL_CreateGPUShader(sdlgpu, &info);
    free(data);

    if (!shader) {
        note("gpu_batch load_shader: SDL_CreateGPUShader failed for %s: %s",
             filename, SDL_GetError());
    }

    return shader;
}

// ============================================================================
// Resource Creation
// ============================================================================

static bool create_quad_vbo(void) {
    SDL_GPUBufferCreateInfo info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(quad_vertices)
    };

    batch_state.quad_vbo = SDL_CreateGPUBuffer(sdlgpu, &info);
    if (!batch_state.quad_vbo) {
        note("gpu_batch create_quad_vbo: Failed: %s", SDL_GetError());
        return false;
    }

    // Upload vertex data
    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(quad_vertices)
    };

    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(sdlgpu, &transfer_info);
    if (!transfer) {
        note("gpu_batch create_quad_vbo: Transfer buffer failed: %s", SDL_GetError());
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(sdlgpu, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        return false;
    }

    memcpy(mapped, quad_vertices, sizeof(quad_vertices));
    SDL_UnmapGPUTransferBuffer(sdlgpu, transfer);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        return false;
    }

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        return false;
    }

    SDL_GPUTransferBufferLocation src = {
        .transfer_buffer = transfer,
        .offset = 0
    };
    SDL_GPUBufferRegion dst = {
        .buffer = batch_state.quad_vbo,
        .offset = 0,
        .size = sizeof(quad_vertices)
    };
    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);

    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);

    return true;
}

static bool create_instance_buffers(void) {
    size_t buffer_size = (size_t)GPU_BATCH_MAX_SPRITES * sizeof(gpu_sprite_instance_t);

    // GPU buffer
    SDL_GPUBufferCreateInfo buf_info = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = (Uint32)buffer_size
    };

    batch_state.instance_buffer = SDL_CreateGPUBuffer(sdlgpu, &buf_info);
    if (!batch_state.instance_buffer) {
        note("gpu_batch create_instance_buffers: GPU buffer failed: %s", SDL_GetError());
        return false;
    }

    // Transfer buffer
    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32)buffer_size
    };

    batch_state.instance_transfer = SDL_CreateGPUTransferBuffer(sdlgpu, &transfer_info);
    if (!batch_state.instance_transfer) {
        note("gpu_batch create_instance_buffers: Transfer buffer failed: %s", SDL_GetError());
        return false;
    }

    // CPU staging buffer
    batch_state.instances = (gpu_sprite_instance_t *)calloc((size_t)GPU_BATCH_MAX_SPRITES,
                                                             sizeof(gpu_sprite_instance_t));
    if (!batch_state.instances) {
        note("gpu_batch create_instance_buffers: CPU buffer alloc failed");
        return false;
    }

    return true;
}

static bool create_sampler(void) {
    SDL_GPUSamplerCreateInfo info = {
        .min_filter = SDL_GPU_FILTER_NEAREST,  // Pixel art - no filtering
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .mip_lod_bias = 0.0f,
        .max_anisotropy = 1.0f,
        .compare_op = SDL_GPU_COMPAREOP_NEVER,
        .min_lod = 0.0f,
        .max_lod = 0.0f,
        .enable_anisotropy = false,
        .enable_compare = false
    };

    batch_state.sampler = SDL_CreateGPUSampler(sdlgpu, &info);
    if (!batch_state.sampler) {
        note("gpu_batch create_sampler: Failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

static bool create_pipeline(void) {
    const char *shader_ext;
    SDL_GPUShaderFormat fmt = get_shader_format();

    switch (fmt) {
        case SDL_GPU_SHADERFORMAT_SPIRV: shader_ext = "spv"; break;
        case SDL_GPU_SHADERFORMAT_DXIL:  shader_ext = "dxil"; break;
        case SDL_GPU_SHADERFORMAT_MSL:   shader_ext = "msl"; break;
        default:
            note("gpu_batch create_pipeline: No supported shader format");
            return false;
    }

    char vs_path[256], ps_path[256];
    snprintf(vs_path, sizeof(vs_path), "res/shaders/compiled/sprite_vs.%s", shader_ext);
    snprintf(ps_path, sizeof(ps_path), "res/shaders/compiled/sprite_ps.%s", shader_ext);

    // Vertex shader: 1 storage buffer (instances), 1 uniform buffer (frame data)
    batch_state.vertex_shader = load_shader(vs_path, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 1);
    if (!batch_state.vertex_shader) {
        note("gpu_batch create_pipeline: Failed to load vertex shader");
        return false;
    }

    // Fragment shader: 1 sampler (sprite texture), no storage buffers, no uniform buffers
    batch_state.fragment_shader = load_shader(ps_path, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0, 0);
    if (!batch_state.fragment_shader) {
        note("gpu_batch create_pipeline: Failed to load fragment shader");
        return false;
    }

    // Vertex layout
    SDL_GPUVertexBufferDescription vb_desc = {
        .slot = 0,
        .pitch = sizeof(batch_vertex_t),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0
    };

    SDL_GPUVertexAttribute vertex_attrs[2] = {
        {
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = offsetof(batch_vertex_t, x)
        },
        {
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = offsetof(batch_vertex_t, u)
        }
    };

    SDL_GPUVertexInputState vertex_input = {
        .vertex_buffer_descriptions = &vb_desc,
        .num_vertex_buffers = 1,
        .vertex_attributes = vertex_attrs,
        .num_vertex_attributes = 2
    };

    // Color attachment with alpha blending
    SDL_GPUColorTargetDescription color_desc = {
        .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,  // Match swapchain format
        .blend_state = {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true,
            .enable_color_write_mask = false
        }
    };

    SDL_GPUGraphicsPipelineTargetInfo target_info = {
        .color_target_descriptions = &color_desc,
        .num_color_targets = 1,
        .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_INVALID,
        .has_depth_stencil_target = false
    };

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {
        .vertex_shader = batch_state.vertex_shader,
        .fragment_shader = batch_state.fragment_shader,
        .vertex_input_state = vertex_input,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_NONE,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .depth_bias_constant_factor = 0.0f,
            .depth_bias_clamp = 0.0f,
            .depth_bias_slope_factor = 0.0f,
            .enable_depth_bias = false,
            .enable_depth_clip = false
        },
        .multisample_state = {
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .sample_mask = 0xFFFFFFFF,
            .enable_mask = false
        },
        .depth_stencil_state = {
            .enable_depth_test = false,
            .enable_depth_write = false,
            .enable_stencil_test = false
        },
        .target_info = target_info
    };

    batch_state.pipeline = SDL_CreateGPUGraphicsPipeline(sdlgpu, &pipeline_info);
    if (!batch_state.pipeline) {
        note("gpu_batch create_pipeline: SDL_CreateGPUGraphicsPipeline failed: %s",
             SDL_GetError());
        return false;
    }

    return true;
}

// ============================================================================
// Public API
// ============================================================================

bool gpu_batch_init(int screen_width, int screen_height) {
    if (!use_gpu_rendering || !sdlgpu) {
        return false;
    }

    if (batch_state.initialized) {
        return true;
    }

    memset(&batch_state, 0, sizeof(batch_state));
    batch_state.screen_width = (float)screen_width;
    batch_state.screen_height = (float)screen_height;

    if (!create_quad_vbo()) {
        gpu_batch_shutdown();
        return false;
    }

    if (!create_instance_buffers()) {
        gpu_batch_shutdown();
        return false;
    }

    if (!create_sampler()) {
        gpu_batch_shutdown();
        return false;
    }

    // Try to create pipeline (may fail if shaders not compiled)
    if (!create_pipeline()) {
        note("gpu_batch_init: Shaders not available, batching disabled");
    }

    batch_state.initialized = true;
    return true;
}

void gpu_batch_shutdown(void) {
    if (!sdlgpu) return;

    if (batch_state.pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(sdlgpu, batch_state.pipeline);
    }
    if (batch_state.vertex_shader) {
        SDL_ReleaseGPUShader(sdlgpu, batch_state.vertex_shader);
    }
    if (batch_state.fragment_shader) {
        SDL_ReleaseGPUShader(sdlgpu, batch_state.fragment_shader);
    }
    if (batch_state.quad_vbo) {
        SDL_ReleaseGPUBuffer(sdlgpu, batch_state.quad_vbo);
    }
    if (batch_state.instance_buffer) {
        SDL_ReleaseGPUBuffer(sdlgpu, batch_state.instance_buffer);
    }
    if (batch_state.instance_transfer) {
        SDL_ReleaseGPUTransferBuffer(sdlgpu, batch_state.instance_transfer);
    }
    if (batch_state.sampler) {
        SDL_ReleaseGPUSampler(sdlgpu, batch_state.sampler);
    }
    if (batch_state.instances) {
        free(batch_state.instances);
    }

    memset(&batch_state, 0, sizeof(batch_state));
    note("gpu_batch_shutdown: Sprite batching system shut down");
}

void gpu_batch_resize(int new_width, int new_height) {
    batch_state.screen_width = (float)new_width;
    batch_state.screen_height = (float)new_height;
}

SDL_GPURenderPass *gpu_batch_begin_frame(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target) {
    if (!batch_state.initialized || !batch_state.pipeline) {
        return NULL;
    }

    // Reset stats
    batch_state.batches_this_frame = 0;
    batch_state.sprites_this_frame = 0;
    batch_state.instance_count = 0;
    batch_state.current_texture = NULL;

    // Begin render pass
    SDL_GPUColorTargetInfo color_target = {
        .texture = target,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
        .cycle = true
    };

    current_pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, NULL);
    current_cmd = cmd;
    batch_state.in_batch = true;

    return current_pass;
}

void gpu_batch_end_frame(void) {
    if (!batch_state.in_batch) return;

    // Flush any remaining sprites
    gpu_batch_flush();

    if (current_pass) {
        SDL_EndGPURenderPass(current_pass);
        current_pass = NULL;
    }

    current_cmd = NULL;
    batch_state.in_batch = false;
}

// ============================================================================
// Batching
// ============================================================================

void gpu_batch_flush(void) {
    if (!batch_state.in_batch || batch_state.instance_count == 0) {
        return;
    }

    if (!current_pass || !current_cmd || !batch_state.current_texture) {
        batch_state.instance_count = 0;
        return;
    }

    // Upload instance data using a SEPARATE command buffer
    // (Can't do a copy pass while a render pass is active on the same cmd buffer)
    SDL_GPUCommandBuffer *upload_cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
    if (!upload_cmd) {
        note("gpu_batch_flush: Failed to acquire upload command buffer");
        batch_state.instance_count = 0;
        return;
    }

    void *mapped = SDL_MapGPUTransferBuffer(sdlgpu, batch_state.instance_transfer, false);
    if (!mapped) {
        note("gpu_batch_flush: Map failed");
        SDL_CancelGPUCommandBuffer(upload_cmd);
        batch_state.instance_count = 0;
        return;
    }

    size_t data_size = (size_t)batch_state.instance_count * sizeof(gpu_sprite_instance_t);
    memcpy(mapped, batch_state.instances, data_size);
    SDL_UnmapGPUTransferBuffer(sdlgpu, batch_state.instance_transfer);

    // Copy to GPU buffer using the upload command buffer
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(upload_cmd);
    if (!copy_pass) {
        note("gpu_batch_flush: Failed to begin copy pass");
        SDL_CancelGPUCommandBuffer(upload_cmd);
        batch_state.instance_count = 0;
        return;
    }

    SDL_GPUTransferBufferLocation src = {
        .transfer_buffer = batch_state.instance_transfer,
        .offset = 0
    };
    SDL_GPUBufferRegion dst = {
        .buffer = batch_state.instance_buffer,
        .offset = 0,
        .size = (Uint32)data_size
    };
    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);
    SDL_EndGPUCopyPass(copy_pass);

    // Submit the upload command buffer with a fence to ensure upload completes before draw
    SDL_GPUFence *upload_fence = SDL_SubmitGPUCommandBufferAndAcquireFence(upload_cmd);
    if (!upload_fence) {
        note("gpu_batch_flush: Failed to submit upload command buffer");
        batch_state.instance_count = 0;
        return;
    }

    // Wait for the upload to complete before proceeding with the draw
    // This ensures the instance buffer data is ready
    if (!SDL_WaitForGPUFences(sdlgpu, true, &upload_fence, 1)) {
        note("gpu_batch_flush: Fence wait failed");
    }
    SDL_ReleaseGPUFence(sdlgpu, upload_fence);

    // Bind pipeline
    SDL_BindGPUGraphicsPipeline(current_pass, batch_state.pipeline);

    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {
        .buffer = batch_state.quad_vbo,
        .offset = 0
    };
    SDL_BindGPUVertexBuffers(current_pass, 0, &vb_binding, 1);

    // Bind instance buffer to vertex shader storage
    SDL_BindGPUVertexStorageBuffers(current_pass, 0, &batch_state.instance_buffer, 1);

    // Bind texture and sampler
    SDL_GPUTextureSamplerBinding tex_binding = {
        .texture = batch_state.current_texture,
        .sampler = batch_state.sampler
    };
    SDL_BindGPUFragmentSamplers(current_pass, 0, &tex_binding, 1);

    // Push frame uniforms (screen size)
    float frame_uniforms[4] = {
        batch_state.screen_width,
        batch_state.screen_height,
        1.0f / batch_state.screen_width,
        1.0f / batch_state.screen_height
    };
    SDL_PushGPUVertexUniformData(current_cmd, 0, frame_uniforms, sizeof(frame_uniforms));

    // Draw instanced
    SDL_DrawGPUPrimitives(current_pass, 6, (Uint32)batch_state.instance_count, 0, 0);

    // Update stats
    batch_state.batches_this_frame++;
    batch_state.sprites_this_frame += batch_state.instance_count;

    // Reset for next batch
    batch_state.instance_count = 0;
}

void gpu_batch_add_sprite(SDL_GPUTexture *texture,
                          const SDL_FRect *dest,
                          const SDL_FRect *src,
                          const gpu_sprite_instance_t *instance) {
    if (!batch_state.in_batch) return;

    // Flush if texture changes or batch is full
    if ((texture != batch_state.current_texture && batch_state.instance_count > 0) ||
        batch_state.instance_count >= GPU_BATCH_MAX_SPRITES) {
        gpu_batch_flush();
    }

    batch_state.current_texture = texture;

    // Add instance
    gpu_sprite_instance_t *inst = &batch_state.instances[batch_state.instance_count];

    if (instance) {
        *inst = *instance;
    } else {
        memset(inst, 0, sizeof(*inst));
        inst->color_r = 1.0f;
        inst->color_g = 1.0f;
        inst->color_b = 1.0f;
        inst->color_a = 1.0f;
        inst->alpha = 255.0f;
    }

    // Override position from dest rect
    inst->dest_x = dest->x;
    inst->dest_y = dest->y;
    inst->dest_w = dest->w;
    inst->dest_h = dest->h;

    // Set texture coordinates
    inst->src_u = src->x;
    inst->src_v = src->y;
    inst->src_w = src->w;
    inst->src_h = src->h;

    batch_state.instance_count++;
}

void gpu_batch_add_sprite_simple(SDL_GPUTexture *texture,
                                  const SDL_FRect *dest,
                                  const SDL_FRect *src,
                                  float alpha) {
    gpu_sprite_instance_t inst = {0};
    inst.color_r = 1.0f;
    inst.color_g = 1.0f;
    inst.color_b = 1.0f;
    inst.color_a = 1.0f;
    inst.alpha = alpha;
    inst.flags = 0;

    gpu_batch_add_sprite(texture, dest, src, &inst);
}

// Set render context from external GPU system
void gpu_batch_set_context(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass) {
    // Flush any pending sprites if changing context
    if (current_cmd != cmd || current_pass != pass) {
        gpu_batch_flush();
    }

    current_cmd = cmd;
    current_pass = pass;

    if (cmd && pass) {
        batch_state.in_batch = true;
        // Reset stats at start of frame
        if (batch_state.instance_count == 0 && batch_state.batches_this_frame == 0) {
            batch_state.sprites_this_frame = 0;
        }
    } else {
        batch_state.in_batch = false;
    }
}

// Check if batch pipeline is ready AND we're actively in a batch context
bool gpu_batch_is_available(void) {
    return batch_state.initialized && batch_state.pipeline != NULL && batch_state.in_batch;
}

// ============================================================================
// Statistics & Debug
// ============================================================================

int gpu_batch_get_batch_count(void) {
    return batch_state.batches_this_frame;
}

int gpu_batch_get_sprite_count(void) {
    return batch_state.sprites_this_frame;
}

void gpu_batch_dump(FILE *fp) {
    if (!fp) return;

    fprintf(fp, "=== GPU Sprite Batch State ===\n");
    fprintf(fp, "Initialized: %s\n", batch_state.initialized ? "yes" : "no");
    fprintf(fp, "Pipeline: %p\n", (void *)batch_state.pipeline);
    fprintf(fp, "In batch: %s\n", batch_state.in_batch ? "yes" : "no");
    fprintf(fp, "Screen: %.0fx%.0f\n",
            (double)batch_state.screen_width,
            (double)batch_state.screen_height);
    fprintf(fp, "\nLast frame:\n");
    fprintf(fp, "  Batches: %d\n", batch_state.batches_this_frame);
    fprintf(fp, "  Sprites: %d\n", batch_state.sprites_this_frame);
    fprintf(fp, "  Current texture: %p\n", (void *)batch_state.current_texture);
    fprintf(fp, "  Instance count: %d\n", batch_state.instance_count);
}
