/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Simple Drawing Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "astonia.h"
#include "sdl/sdl_gpu_draw.h"
#include "sdl/sdl_gpu.h"
#include "helper/helper.h"

// State
static struct {
    // Sprite pipeline
    SDL_GPUGraphicsPipeline *sprite_pipeline;
    SDL_GPUShader *sprite_vs;
    SDL_GPUShader *sprite_fs;

    // Primitive pipeline
    SDL_GPUGraphicsPipeline *prim_pipeline;
    SDL_GPUShader *prim_vs;
    SDL_GPUShader *prim_fs;

    // Line pipeline
    SDL_GPUGraphicsPipeline *line_pipeline;
    SDL_GPUShader *line_vs;
    SDL_GPUShader *line_fs;
    SDL_GPUBuffer *line_vbo;

    // Shared resources
    SDL_GPUBuffer *quad_vbo;
    SDL_GPUSampler *sampler;

    // Screen dimensions
    float screen_width;
    float screen_height;

    bool initialized;
    bool sprite_ready;
    bool prim_ready;
    bool line_ready;
} draw_state = {0};

// Quad vertices
typedef struct {
    float x, y;
    float u, v;
} draw_vertex_t;

static const draw_vertex_t quad_vertices[6] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 1.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    {0.0f, 1.0f, 0.0f, 1.0f},
};

// Push constant structure for sprite shader
typedef struct {
    float dest_x, dest_y, dest_w, dest_h;
    float src_u, src_v, src_w, src_h;
    float color_r, color_g, color_b, color_a;
    float screen_w, screen_h;
    float padding[2];
} sprite_push_constants_t;

// Push constant structure for primitive shader
typedef struct {
    float dest_x, dest_y, dest_w, dest_h;
    float color_r, color_g, color_b, color_a;
    float screen_w, screen_h;
    float padding[2];
} prim_push_constants_t;

// Push constant structure for line shader
typedef struct {
    float start_x, start_y, end_x, end_y;
    float color_r, color_g, color_b, color_a;
    float screen_w, screen_h;
    float padding[2];
} line_push_constants_t;

// Line vertices (just 2 points: t=0 and t=1 for interpolation)
static const draw_vertex_t line_vertices[2] = {
    {0.0f, 0.0f, 0.0f, 0.0f},  // Start point (t=0)
    {1.0f, 0.0f, 0.0f, 0.0f},  // End point (t=1)
};

// ============================================================================
// Helpers
// ============================================================================

static SDL_GPUShaderFormat get_shader_format(void) {
    if (!sdlgpu) return 0;
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(sdlgpu);
    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) return SDL_GPU_SHADERFORMAT_SPIRV;
    if (formats & SDL_GPU_SHADERFORMAT_DXIL) return SDL_GPU_SHADERFORMAT_DXIL;
    if (formats & SDL_GPU_SHADERFORMAT_MSL) return SDL_GPU_SHADERFORMAT_MSL;
    return 0;
}

static SDL_GPUShader *load_shader(const char *filename, SDL_GPUShaderStage stage,
                                   Uint32 num_samplers, Uint32 num_uniform_buffers) {
    if (!sdlgpu) return NULL;

    FILE *f = fopen(filename, "rb");
    if (!f) {
        note("gpu_draw load_shader: Cannot open %s", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = (char *)malloc(size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    if (fread(data, 1, size, f) != size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    SDL_GPUShaderFormat fmt = get_shader_format();
    SDL_GPUShaderCreateInfo info = {
        .code = (const Uint8 *)data,
        .code_size = size,
        .entrypoint = (fmt == SDL_GPU_SHADERFORMAT_SPIRV) ? "main" :
                      (stage == SDL_GPU_SHADERSTAGE_VERTEX) ? "VSMain" : "PSMain",
        .format = fmt,
        .stage = stage,
        .num_samplers = num_samplers,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = num_uniform_buffers
    };

    SDL_GPUShader *shader = SDL_CreateGPUShader(sdlgpu, &info);
    free(data);

    if (!shader) {
        note("gpu_draw load_shader: Failed for %s: %s", filename, SDL_GetError());
    }

    return shader;
}

static bool create_quad_vbo(void) {
    SDL_GPUBufferCreateInfo info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(quad_vertices)
    };

    draw_state.quad_vbo = SDL_CreateGPUBuffer(sdlgpu, &info);
    if (!draw_state.quad_vbo) return false;

    // Upload
    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(quad_vertices)
    };

    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(sdlgpu, &transfer_info);
    if (!transfer) return false;

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

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    if (!copy) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        return false;
    }

    SDL_GPUTransferBufferLocation src = { .transfer_buffer = transfer, .offset = 0 };
    SDL_GPUBufferRegion dst = { .buffer = draw_state.quad_vbo, .offset = 0, .size = sizeof(quad_vertices) };
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);

    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);

    return true;
}

static bool create_sampler(void) {
    SDL_GPUSamplerCreateInfo info = {
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };

    draw_state.sampler = SDL_CreateGPUSampler(sdlgpu, &info);
    return draw_state.sampler != NULL;
}

static bool create_sprite_pipeline(void) {
    const char *ext = (get_shader_format() == SDL_GPU_SHADERFORMAT_SPIRV) ? "spv" : "dxil";
    char vs_path[256], fs_path[256];
    snprintf(vs_path, sizeof(vs_path), "res/shaders/compiled/sprite_simple_vs.%s", ext);
    snprintf(fs_path, sizeof(fs_path), "res/shaders/compiled/sprite_simple_ps.%s", ext);

    // Vertex shader: no samplers, 1 uniform buffer (sprite data)
    draw_state.sprite_vs = load_shader(vs_path, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    if (!draw_state.sprite_vs) return false;

    // Fragment shader: 1 sampler, no uniform buffers
    draw_state.sprite_fs = load_shader(fs_path, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    if (!draw_state.sprite_fs) return false;

    SDL_GPUVertexBufferDescription vb_desc = {
        .slot = 0,
        .pitch = sizeof(draw_vertex_t),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
    };

    SDL_GPUVertexAttribute attrs[2] = {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(draw_vertex_t, x) },
        { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(draw_vertex_t, u) },
    };

    SDL_GPUVertexInputState vertex_input = {
        .vertex_buffer_descriptions = &vb_desc,
        .num_vertex_buffers = 1,
        .vertex_attributes = attrs,
        .num_vertex_attributes = 2,
    };

    // Use swapchain format (B8G8R8A8) for direct rendering
    SDL_GPUColorTargetDescription color_desc = {
        .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
        .blend_state = {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true,
        },
    };

    SDL_GPUGraphicsPipelineTargetInfo target_info = {
        .color_target_descriptions = &color_desc,
        .num_color_targets = 1,
    };

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {
        .vertex_shader = draw_state.sprite_vs,
        .fragment_shader = draw_state.sprite_fs,
        .vertex_input_state = vertex_input,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = { .fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE },
        .multisample_state = { .sample_count = SDL_GPU_SAMPLECOUNT_1, .sample_mask = 0xFFFFFFFF },
        .target_info = target_info,
    };

    draw_state.sprite_pipeline = SDL_CreateGPUGraphicsPipeline(sdlgpu, &pipeline_info);
    if (!draw_state.sprite_pipeline) {
        note("gpu_draw: Sprite pipeline failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

static bool create_primitive_pipeline(void) {
    const char *ext = (get_shader_format() == SDL_GPU_SHADERFORMAT_SPIRV) ? "spv" : "dxil";
    char vs_path[256], fs_path[256];
    snprintf(vs_path, sizeof(vs_path), "res/shaders/compiled/primitive_vs.%s", ext);
    snprintf(fs_path, sizeof(fs_path), "res/shaders/compiled/primitive_ps.%s", ext);

    // Vertex shader: no samplers, 1 uniform buffer (primitive data)
    draw_state.prim_vs = load_shader(vs_path, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    if (!draw_state.prim_vs) return false;

    // Fragment shader: no samplers, no uniform buffers
    draw_state.prim_fs = load_shader(fs_path, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!draw_state.prim_fs) return false;

    // Primitives use the same quad VBO as sprites - only position is used
    SDL_GPUVertexBufferDescription vb_desc = {
        .slot = 0,
        .pitch = sizeof(draw_vertex_t),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
    };

    // Only position attribute - color comes from uniform
    SDL_GPUVertexAttribute attrs[1] = {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(draw_vertex_t, x) },
    };

    SDL_GPUVertexInputState vertex_input = {
        .vertex_buffer_descriptions = &vb_desc,
        .num_vertex_buffers = 1,
        .vertex_attributes = attrs,
        .num_vertex_attributes = 1,
    };

    SDL_GPUColorTargetDescription color_desc = {
        .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
        .blend_state = {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true,
        },
    };

    SDL_GPUGraphicsPipelineTargetInfo target_info = {
        .color_target_descriptions = &color_desc,
        .num_color_targets = 1,
    };

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {
        .vertex_shader = draw_state.prim_vs,
        .fragment_shader = draw_state.prim_fs,
        .vertex_input_state = vertex_input,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = { .fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE },
        .multisample_state = { .sample_count = SDL_GPU_SAMPLECOUNT_1, .sample_mask = 0xFFFFFFFF },
        .target_info = target_info,
    };

    draw_state.prim_pipeline = SDL_CreateGPUGraphicsPipeline(sdlgpu, &pipeline_info);
    if (!draw_state.prim_pipeline) {
        note("gpu_draw: Primitive pipeline failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

static bool create_line_vbo(void) {
    SDL_GPUBufferCreateInfo info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(line_vertices)
    };

    draw_state.line_vbo = SDL_CreateGPUBuffer(sdlgpu, &info);
    if (!draw_state.line_vbo) return false;

    // Upload
    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(line_vertices)
    };

    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(sdlgpu, &transfer_info);
    if (!transfer) return false;

    void *mapped = SDL_MapGPUTransferBuffer(sdlgpu, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        return false;
    }

    memcpy(mapped, line_vertices, sizeof(line_vertices));
    SDL_UnmapGPUTransferBuffer(sdlgpu, transfer);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        return false;
    }

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    if (!copy) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        return false;
    }

    SDL_GPUTransferBufferLocation src = { .transfer_buffer = transfer, .offset = 0 };
    SDL_GPUBufferRegion dst = { .buffer = draw_state.line_vbo, .offset = 0, .size = sizeof(line_vertices) };
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);

    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);

    return true;
}

static bool create_line_pipeline(void) {
    const char *ext = (get_shader_format() == SDL_GPU_SHADERFORMAT_SPIRV) ? "spv" : "dxil";
    char vs_path[256], fs_path[256];
    snprintf(vs_path, sizeof(vs_path), "res/shaders/compiled/line_vs.%s", ext);
    snprintf(fs_path, sizeof(fs_path), "res/shaders/compiled/line_ps.%s", ext);

    // Vertex shader: no samplers, 1 uniform buffer (line data)
    draw_state.line_vs = load_shader(vs_path, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    if (!draw_state.line_vs) return false;

    // Fragment shader: no samplers, no uniform buffers
    draw_state.line_fs = load_shader(fs_path, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!draw_state.line_fs) return false;

    // Line VBO uses same vertex format as quad but only position matters
    SDL_GPUVertexBufferDescription vb_desc = {
        .slot = 0,
        .pitch = sizeof(draw_vertex_t),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
    };

    // Only position attribute
    SDL_GPUVertexAttribute attrs[1] = {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(draw_vertex_t, x) },
    };

    SDL_GPUVertexInputState vertex_input = {
        .vertex_buffer_descriptions = &vb_desc,
        .num_vertex_buffers = 1,
        .vertex_attributes = attrs,
        .num_vertex_attributes = 1,
    };

    SDL_GPUColorTargetDescription color_desc = {
        .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
        .blend_state = {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true,
        },
    };

    SDL_GPUGraphicsPipelineTargetInfo target_info = {
        .color_target_descriptions = &color_desc,
        .num_color_targets = 1,
    };

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {
        .vertex_shader = draw_state.line_vs,
        .fragment_shader = draw_state.line_fs,
        .vertex_input_state = vertex_input,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST,
        .rasterizer_state = { .fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE },
        .multisample_state = { .sample_count = SDL_GPU_SAMPLECOUNT_1, .sample_mask = 0xFFFFFFFF },
        .target_info = target_info,
    };

    draw_state.line_pipeline = SDL_CreateGPUGraphicsPipeline(sdlgpu, &pipeline_info);
    if (!draw_state.line_pipeline) {
        note("gpu_draw: Line pipeline failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

// ============================================================================
// Public API
// ============================================================================

bool gpu_draw_init(int screen_width, int screen_height) {
    if (!use_gpu_rendering || !sdlgpu) {
        return false;
    }

    if (draw_state.initialized) {
        return true;
    }

    memset(&draw_state, 0, sizeof(draw_state));
    draw_state.screen_width = (float)screen_width;
    draw_state.screen_height = (float)screen_height;

    if (!create_quad_vbo()) {
        note("gpu_draw_init: quad VBO failed");
        gpu_draw_shutdown();
        return false;
    }

    if (!create_sampler()) {
        note("gpu_draw_init: sampler failed");
        gpu_draw_shutdown();
        return false;
    }

    // Try sprite pipeline
    if (create_sprite_pipeline()) {
        draw_state.sprite_ready = true;
    } else {
        note("gpu_draw_init: sprite pipeline not available");
    }

    // Try primitive pipeline
    if (create_primitive_pipeline()) {
        draw_state.prim_ready = true;
    } else {
        note("gpu_draw_init: primitive pipeline not available");
    }

    // Try line pipeline
    if (create_line_vbo() && create_line_pipeline()) {
        draw_state.line_ready = true;
    } else {
        note("gpu_draw_init: line pipeline not available");
    }

    draw_state.initialized = true;
    return true;
}

void gpu_draw_shutdown(void) {
    if (!sdlgpu) return;

    if (draw_state.sprite_pipeline) SDL_ReleaseGPUGraphicsPipeline(sdlgpu, draw_state.sprite_pipeline);
    if (draw_state.sprite_vs) SDL_ReleaseGPUShader(sdlgpu, draw_state.sprite_vs);
    if (draw_state.sprite_fs) SDL_ReleaseGPUShader(sdlgpu, draw_state.sprite_fs);
    if (draw_state.prim_pipeline) SDL_ReleaseGPUGraphicsPipeline(sdlgpu, draw_state.prim_pipeline);
    if (draw_state.prim_vs) SDL_ReleaseGPUShader(sdlgpu, draw_state.prim_vs);
    if (draw_state.prim_fs) SDL_ReleaseGPUShader(sdlgpu, draw_state.prim_fs);
    if (draw_state.line_pipeline) SDL_ReleaseGPUGraphicsPipeline(sdlgpu, draw_state.line_pipeline);
    if (draw_state.line_vs) SDL_ReleaseGPUShader(sdlgpu, draw_state.line_vs);
    if (draw_state.line_fs) SDL_ReleaseGPUShader(sdlgpu, draw_state.line_fs);
    if (draw_state.line_vbo) SDL_ReleaseGPUBuffer(sdlgpu, draw_state.line_vbo);
    if (draw_state.quad_vbo) SDL_ReleaseGPUBuffer(sdlgpu, draw_state.quad_vbo);
    if (draw_state.sampler) SDL_ReleaseGPUSampler(sdlgpu, draw_state.sampler);

    memset(&draw_state, 0, sizeof(draw_state));
}

void gpu_draw_resize(int new_width, int new_height) {
    draw_state.screen_width = (float)new_width;
    draw_state.screen_height = (float)new_height;
}

bool gpu_draw_is_available(void) {
    return draw_state.initialized && draw_state.sprite_ready;
}

void gpu_draw_texture(SDL_GPUTexture *texture,
                      const SDL_FRect *dest,
                      const SDL_FRect *src,
                      int tex_width, int tex_height,
                      const float *color_mod,
                      int alpha) {
    if (!draw_state.sprite_ready || !texture) return;

    SDL_GPURenderPass *pass = gpu_get_render_pass();
    SDL_GPUCommandBuffer *cmd = gpu_get_command_buffer();
    if (!pass || !cmd) return;

    // Bind pipeline
    SDL_BindGPUGraphicsPipeline(pass, draw_state.sprite_pipeline);

    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = { .buffer = draw_state.quad_vbo, .offset = 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);

    // Bind texture
    SDL_GPUTextureSamplerBinding tex_binding = { .texture = texture, .sampler = draw_state.sampler };
    SDL_BindGPUFragmentSamplers(pass, 0, &tex_binding, 1);

    // Calculate UV coordinates
    float u = 0.0f, v = 0.0f, uw = 1.0f, vh = 1.0f;
    if (src && tex_width > 0 && tex_height > 0) {
        u = src->x / (float)tex_width;
        v = src->y / (float)tex_height;
        uw = src->w / (float)tex_width;
        vh = src->h / (float)tex_height;
    }

    // Get current swapchain dimensions (may differ from cached values)
    int sw_width, sw_height;
    gpu_get_swapchain_size(&sw_width, &sw_height);
    float screen_w = (sw_width > 0) ? (float)sw_width : draw_state.screen_width;
    float screen_h = (sw_height > 0) ? (float)sw_height : draw_state.screen_height;

    // Push constants
    sprite_push_constants_t pc = {
        .dest_x = dest->x,
        .dest_y = dest->y,
        .dest_w = dest->w,
        .dest_h = dest->h,
        .src_u = u,
        .src_v = v,
        .src_w = uw,
        .src_h = vh,
        .color_r = color_mod ? color_mod[0] : 1.0f,
        .color_g = color_mod ? color_mod[1] : 1.0f,
        .color_b = color_mod ? color_mod[2] : 1.0f,
        .color_a = (float)alpha / 255.0f,
        .screen_w = screen_w,
        .screen_h = screen_h,
    };

    SDL_PushGPUVertexUniformData(cmd, 0, &pc, sizeof(pc));

    // Draw
    SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);

    // Track draw count for debugging
    gpu_debug_increment_draw_count();
}

void gpu_draw_rect(float x, float y, float w, float h,
                   float r, float g, float b, float a) {
    if (!draw_state.prim_ready || !draw_state.prim_pipeline) {
        return;
    }

    SDL_GPUCommandBuffer *cmd = gpu_get_command_buffer();
    SDL_GPURenderPass *pass = gpu_get_render_pass();
    if (!cmd || !pass) {
        return;
    }

    // Bind primitive pipeline
    SDL_BindGPUGraphicsPipeline(pass, draw_state.prim_pipeline);

    // Bind vertex buffer
    SDL_GPUBufferBinding vb_bind = { .buffer = draw_state.quad_vbo, .offset = 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb_bind, 1);

    // Get current swapchain dimensions
    int sw_width, sw_height;
    gpu_get_swapchain_size(&sw_width, &sw_height);
    float screen_w = (sw_width > 0) ? (float)sw_width : draw_state.screen_width;
    float screen_h = (sw_height > 0) ? (float)sw_height : draw_state.screen_height;

    // Push uniform data
    prim_push_constants_t pc = {
        .dest_x = x,
        .dest_y = y,
        .dest_w = w,
        .dest_h = h,
        .color_r = r,
        .color_g = g,
        .color_b = b,
        .color_a = a,
        .screen_w = screen_w,
        .screen_h = screen_h,
    };

    SDL_PushGPUVertexUniformData(cmd, 0, &pc, sizeof(pc));

    // Draw
    SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);

    // Track draw count for debugging
    gpu_debug_increment_draw_count();
}

// Check if primitive drawing is available
bool gpu_draw_prim_is_available(void) {
    return draw_state.initialized && draw_state.prim_ready;
}

// Check if line drawing is available
bool gpu_draw_line_is_available(void) {
    return draw_state.initialized && draw_state.line_ready;
}

// Draw a line
void gpu_draw_line(float x1, float y1, float x2, float y2,
                   float r, float g, float b, float a) {
    if (!draw_state.line_ready || !draw_state.line_pipeline) {
        return;
    }

    SDL_GPUCommandBuffer *cmd = gpu_get_command_buffer();
    SDL_GPURenderPass *pass = gpu_get_render_pass();
    if (!cmd || !pass) {
        return;
    }

    // Bind line pipeline
    SDL_BindGPUGraphicsPipeline(pass, draw_state.line_pipeline);

    // Bind vertex buffer
    SDL_GPUBufferBinding vb_bind = { .buffer = draw_state.line_vbo, .offset = 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb_bind, 1);

    // Get current swapchain dimensions
    int sw_width, sw_height;
    gpu_get_swapchain_size(&sw_width, &sw_height);
    float screen_w = (sw_width > 0) ? (float)sw_width : draw_state.screen_width;
    float screen_h = (sw_height > 0) ? (float)sw_height : draw_state.screen_height;

    // Push uniform data
    line_push_constants_t pc = {
        .start_x = x1,
        .start_y = y1,
        .end_x = x2,
        .end_y = y2,
        .color_r = r,
        .color_g = g,
        .color_b = b,
        .color_a = a,
        .screen_w = screen_w,
        .screen_h = screen_h,
    };

    SDL_PushGPUVertexUniformData(cmd, 0, &pc, sizeof(pc));

    // Draw line (2 vertices for line list)
    SDL_DrawGPUPrimitives(pass, 2, 1, 0, 0);

    // Track draw count for debugging
    gpu_debug_increment_draw_count();
}
