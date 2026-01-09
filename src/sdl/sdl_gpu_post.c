/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Post-Processing Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "astonia.h"
#include "sdl/sdl_gpu_post.h"
#include "sdl/sdl_gpu.h"
#include "helper/helper.h"

// Global post-processing state
static gpu_postfx_state_t postfx_state = {0};

// Fullscreen quad vertices (position + texcoord)
// NDC coordinates: -1 to 1
// Texcoords: 0 to 1
typedef struct postfx_vertex {
    float x, y;     // Position (NDC)
    float u, v;     // Texture coordinates
} postfx_vertex_t;

static const postfx_vertex_t quad_vertices[6] = {
    // Triangle 1
    {-1.0f, -1.0f, 0.0f, 1.0f},  // Bottom-left
    { 1.0f, -1.0f, 1.0f, 1.0f},  // Bottom-right
    { 1.0f,  1.0f, 1.0f, 0.0f},  // Top-right
    // Triangle 2
    {-1.0f, -1.0f, 0.0f, 1.0f},  // Bottom-left
    { 1.0f,  1.0f, 1.0f, 0.0f},  // Top-right
    {-1.0f,  1.0f, 0.0f, 0.0f},  // Top-left
};

// ============================================================================
// Shader Loading
// ============================================================================

// Helper to determine shader format based on platform
static SDL_GPUShaderFormat get_shader_format(void) {
    if (!sdlgpu) return 0;

    // Get supported shader formats from the device
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(sdlgpu);

    // Prefer SPIR-V (Vulkan), then DXIL (D3D12), then MSL (Metal)
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

// Load shader from compiled binary file
static SDL_GPUShader *load_shader(const char *filename, SDL_GPUShaderStage stage,
                                   Uint32 num_samplers, Uint32 num_uniform_buffers) {
    if (!sdlgpu) return NULL;

    // Read shader file
    char *data = NULL;
    size_t size = 0;
    FILE *f = fopen(filename, "rb");
    if (!f) {
        note("load_shader: Cannot open %s", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    data = (char *)malloc(size);
    if (!data) {
        fclose(f);
        note("load_shader: Out of memory for %s", filename);
        return NULL;
    }

    if (fread(data, 1, size, f) != size) {
        free(data);
        fclose(f);
        note("load_shader: Failed to read %s", filename);
        return NULL;
    }
    fclose(f);

    // Create shader
    // SPIR-V from GLSL uses "main" as entry point; HLSL/DXIL uses "VSMain"/"PSMain"
    SDL_GPUShaderFormat fmt = get_shader_format();
    const char *entrypoint;
    if (fmt == SDL_GPU_SHADERFORMAT_SPIRV) {
        entrypoint = "main";  // GLSL entry point
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
        .num_storage_buffers = 0,
        .num_uniform_buffers = num_uniform_buffers
    };

    SDL_GPUShader *shader = SDL_CreateGPUShader(sdlgpu, &info);
    free(data);

    if (!shader) {
        note("load_shader: SDL_CreateGPUShader failed for %s: %s", filename, SDL_GetError());
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

    postfx_state.quad_vbo = SDL_CreateGPUBuffer(sdlgpu, &info);
    if (!postfx_state.quad_vbo) {
        note("create_quad_vbo: SDL_CreateGPUBuffer failed: %s", SDL_GetError());
        return false;
    }

    // Upload vertex data
    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(quad_vertices)
    };

    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(sdlgpu, &transfer_info);
    if (!transfer) {
        note("create_quad_vbo: Transfer buffer failed: %s", SDL_GetError());
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(sdlgpu, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        note("create_quad_vbo: Map failed: %s", SDL_GetError());
        return false;
    }

    memcpy(mapped, quad_vertices, sizeof(quad_vertices));
    SDL_UnmapGPUTransferBuffer(sdlgpu, transfer);

    // Upload to GPU
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        note("create_quad_vbo: Acquire command buffer failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        note("create_quad_vbo: Begin copy pass failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferLocation src = {
        .transfer_buffer = transfer,
        .offset = 0
    };
    SDL_GPUBufferRegion dst = {
        .buffer = postfx_state.quad_vbo,
        .offset = 0,
        .size = sizeof(quad_vertices)
    };
    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);

    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);

    return true;
}

static bool create_scene_texture(int width, int height) {
    // Release old texture if resizing
    if (postfx_state.scene_texture) {
        SDL_ReleaseGPUTexture(sdlgpu, postfx_state.scene_texture);
        postfx_state.scene_texture = NULL;
    }

    SDL_GPUTextureCreateInfo info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,  // Match swapchain format
        .width = (Uint32)width,
        .height = (Uint32)height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .sample_count = SDL_GPU_SAMPLECOUNT_1
    };

    postfx_state.scene_texture = SDL_CreateGPUTexture(sdlgpu, &info);
    if (!postfx_state.scene_texture) {
        note("create_scene_texture: SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return false;
    }

    postfx_state.scene_width = width;
    postfx_state.scene_height = height;

    return true;
}

static bool create_sampler(void) {
    SDL_GPUSamplerCreateInfo info = {
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
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

    postfx_state.sampler = SDL_CreateGPUSampler(sdlgpu, &info);
    if (!postfx_state.sampler) {
        note("create_sampler: SDL_CreateGPUSampler failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

static bool create_pipeline(void) {
    // Determine shader extension based on platform
    const char *shader_ext;
    SDL_GPUShaderFormat fmt = get_shader_format();

    switch (fmt) {
        case SDL_GPU_SHADERFORMAT_SPIRV: shader_ext = "spv"; break;
        case SDL_GPU_SHADERFORMAT_DXIL:  shader_ext = "dxil"; break;
        case SDL_GPU_SHADERFORMAT_MSL:   shader_ext = "msl"; break;
        default:
            note("create_pipeline: No supported shader format");
            return false;
    }

    // Load shaders
    char vs_path[256], ps_path[256];
    snprintf(vs_path, sizeof(vs_path), "res/shaders/compiled/postfx_vs.%s", shader_ext);
    snprintf(ps_path, sizeof(ps_path), "res/shaders/compiled/postfx_ps.%s", shader_ext);

    postfx_state.vertex_shader = load_shader(vs_path, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    if (!postfx_state.vertex_shader) {
        note("create_pipeline: Failed to load vertex shader %s", vs_path);
        return false;
    }

    // Fragment shader needs 1 sampler and 1 uniform buffer
    postfx_state.fragment_shader = load_shader(ps_path, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    if (!postfx_state.fragment_shader) {
        note("create_pipeline: Failed to load fragment shader %s", ps_path);
        return false;
    }

    // Vertex layout for fullscreen quad
    SDL_GPUVertexBufferDescription vb_desc = {
        .slot = 0,
        .pitch = sizeof(postfx_vertex_t),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0
    };

    SDL_GPUVertexAttribute vertex_attrs[2] = {
        {
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = offsetof(postfx_vertex_t, x)
        },
        {
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = offsetof(postfx_vertex_t, u)
        }
    };

    SDL_GPUVertexInputState vertex_input = {
        .vertex_buffer_descriptions = &vb_desc,
        .num_vertex_buffers = 1,
        .vertex_attributes = vertex_attrs,
        .num_vertex_attributes = 2
    };

    // Color attachment for swapchain output
    SDL_GPUColorTargetDescription color_desc = {
        .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,  // Swapchain format
        .blend_state = {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = false,
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
        .vertex_shader = postfx_state.vertex_shader,
        .fragment_shader = postfx_state.fragment_shader,
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

    postfx_state.pipeline = SDL_CreateGPUGraphicsPipeline(sdlgpu, &pipeline_info);
    if (!postfx_state.pipeline) {
        note("create_pipeline: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

// ============================================================================
// Public API
// ============================================================================

bool gpu_postfx_init(int screen_width, int screen_height) {
    if (!use_gpu_rendering || !sdlgpu) {
        return false;
    }

    if (postfx_state.initialized) {
        return true;
    }

    memset(&postfx_state, 0, sizeof(postfx_state));

    // Initialize default parameters
    postfx_state.params.screen_width = (float)screen_width;
    postfx_state.params.screen_height = (float)screen_height;
    postfx_state.params.vignette_intensity = 0.0f;
    postfx_state.params.vignette_radius = 0.3f;
    postfx_state.params.tint_r = 0.0f;
    postfx_state.params.tint_g = 0.0f;
    postfx_state.params.tint_b = 0.0f;
    postfx_state.params.tint_intensity = 0.0f;
    postfx_state.params.brightness = 0.0f;
    postfx_state.params.contrast = 1.0f;
    postfx_state.params.saturation = 1.0f;

    // Create resources
    if (!create_quad_vbo()) {
        gpu_postfx_shutdown();
        return false;
    }

    if (!create_scene_texture(screen_width, screen_height)) {
        gpu_postfx_shutdown();
        return false;
    }

    if (!create_sampler()) {
        gpu_postfx_shutdown();
        return false;
    }

    // Try to create pipeline (may fail if shaders not compiled yet)
    // Pipeline can be created later via gpu_postfx_load_shaders()
    if (create_pipeline()) {
        postfx_state.enabled = true;
    } else {
        postfx_state.enabled = false;
        note("gpu_postfx_init: Shaders not available, post-processing disabled");
    }

    postfx_state.initialized = true;
    return true;
}

void gpu_postfx_shutdown(void) {
    if (!sdlgpu) return;

    if (postfx_state.pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(sdlgpu, postfx_state.pipeline);
    }
    if (postfx_state.vertex_shader) {
        SDL_ReleaseGPUShader(sdlgpu, postfx_state.vertex_shader);
    }
    if (postfx_state.fragment_shader) {
        SDL_ReleaseGPUShader(sdlgpu, postfx_state.fragment_shader);
    }
    if (postfx_state.scene_texture) {
        SDL_ReleaseGPUTexture(sdlgpu, postfx_state.scene_texture);
    }
    if (postfx_state.quad_vbo) {
        SDL_ReleaseGPUBuffer(sdlgpu, postfx_state.quad_vbo);
    }
    if (postfx_state.sampler) {
        SDL_ReleaseGPUSampler(sdlgpu, postfx_state.sampler);
    }

    memset(&postfx_state, 0, sizeof(postfx_state));
    note("gpu_postfx_shutdown: Post-processing system shut down");
}

bool gpu_postfx_resize(int new_width, int new_height) {
    if (!postfx_state.initialized) return false;

    postfx_state.params.screen_width = (float)new_width;
    postfx_state.params.screen_height = (float)new_height;

    return create_scene_texture(new_width, new_height);
}

SDL_GPURenderPass *gpu_postfx_begin_scene(SDL_GPUCommandBuffer *cmd) {
    if (!postfx_state.initialized || !postfx_state.scene_texture) {
        return NULL;
    }

    SDL_GPUColorTargetInfo color_target = {
        .texture = postfx_state.scene_texture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
        .cycle = true
    };

    return SDL_BeginGPURenderPass(cmd, &color_target, 1, NULL);
}

void gpu_postfx_end_scene(SDL_GPURenderPass *pass) {
    if (pass) {
        SDL_EndGPURenderPass(pass);
    }
}

void gpu_postfx_present(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *swapchain_texture) {
    if (!postfx_state.initialized || !postfx_state.pipeline || !swapchain_texture) {
        return;
    }

    // Begin render pass to swapchain
    SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
        .load_op = SDL_GPU_LOADOP_DONT_CARE,  // We'll overwrite everything
        .store_op = SDL_GPU_STOREOP_STORE,
        .cycle = false
    };

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, NULL);
    if (!pass) {
        note("gpu_postfx_present: Failed to begin render pass");
        return;
    }

    // Bind pipeline
    SDL_BindGPUGraphicsPipeline(pass, postfx_state.pipeline);

    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {
        .buffer = postfx_state.quad_vbo,
        .offset = 0
    };
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);

    // Bind scene texture and sampler
    SDL_GPUTextureSamplerBinding tex_binding = {
        .texture = postfx_state.scene_texture,
        .sampler = postfx_state.sampler
    };
    SDL_BindGPUFragmentSamplers(pass, 0, &tex_binding, 1);

    // Push uniform data
    SDL_PushGPUFragmentUniformData(cmd, 0, &postfx_state.params, sizeof(postfx_state.params));

    // Draw fullscreen quad
    SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);

    SDL_EndGPURenderPass(pass);
}

// ============================================================================
// Effect Configuration
// ============================================================================

void gpu_postfx_set_enabled(bool enabled) {
    postfx_state.enabled = enabled;
}

bool gpu_postfx_is_enabled(void) {
    // TODO: Re-enable once sprite rendering is stable
    return false;  // Temporarily disabled for debugging
    // return postfx_state.enabled && postfx_state.initialized;
}

void gpu_postfx_set_vignette(float intensity, float radius) {
    postfx_state.params.vignette_intensity = intensity;
    postfx_state.params.vignette_radius = radius;
}

void gpu_postfx_clear_vignette(void) {
    postfx_state.params.vignette_intensity = 0.0f;
}

void gpu_postfx_set_tint(uint16_t color, uint8_t intensity) {
    // Convert 16-bit color (RGB565) to float RGB
    float r = (float)((color >> 11) & 0x1F) / 31.0f;
    float g = (float)((color >> 5) & 0x3F) / 63.0f;
    float b = (float)(color & 0x1F) / 31.0f;

    postfx_state.params.tint_r = r;
    postfx_state.params.tint_g = g;
    postfx_state.params.tint_b = b;
    postfx_state.params.tint_intensity = (float)intensity / 255.0f;
}

void gpu_postfx_set_tint_rgb(float r, float g, float b, float intensity) {
    postfx_state.params.tint_r = r;
    postfx_state.params.tint_g = g;
    postfx_state.params.tint_b = b;
    postfx_state.params.tint_intensity = intensity;
}

void gpu_postfx_clear_tint(void) {
    postfx_state.params.tint_intensity = 0.0f;
}

void gpu_postfx_set_brightness(float brightness) {
    postfx_state.params.brightness = brightness;
}

void gpu_postfx_set_contrast(float contrast) {
    postfx_state.params.contrast = contrast;
}

void gpu_postfx_set_saturation(float saturation) {
    postfx_state.params.saturation = saturation;
}

void gpu_postfx_reset(void) {
    postfx_state.params.vignette_intensity = 0.0f;
    postfx_state.params.vignette_radius = 0.3f;
    postfx_state.params.tint_r = 0.0f;
    postfx_state.params.tint_g = 0.0f;
    postfx_state.params.tint_b = 0.0f;
    postfx_state.params.tint_intensity = 0.0f;
    postfx_state.params.brightness = 0.0f;
    postfx_state.params.contrast = 1.0f;
    postfx_state.params.saturation = 1.0f;
}

// ============================================================================
// Utility
// ============================================================================

SDL_GPUTexture *gpu_postfx_get_scene_texture(void) {
    return postfx_state.scene_texture;
}

void gpu_postfx_dump(FILE *fp) {
    if (!fp) return;

    fprintf(fp, "=== GPU Post-Processing State ===\n");
    fprintf(fp, "Initialized: %s\n", postfx_state.initialized ? "yes" : "no");
    fprintf(fp, "Enabled: %s\n", postfx_state.enabled ? "yes" : "no");
    fprintf(fp, "Scene texture: %p (%dx%d)\n",
            (void *)postfx_state.scene_texture,
            postfx_state.scene_width,
            postfx_state.scene_height);
    fprintf(fp, "Pipeline: %p\n", (void *)postfx_state.pipeline);
    fprintf(fp, "\nParameters:\n");
    fprintf(fp, "  Vignette: intensity=%.2f, radius=%.2f\n",
            (double)postfx_state.params.vignette_intensity,
            (double)postfx_state.params.vignette_radius);
    fprintf(fp, "  Tint: rgb=(%.2f,%.2f,%.2f), intensity=%.2f\n",
            (double)postfx_state.params.tint_r,
            (double)postfx_state.params.tint_g,
            (double)postfx_state.params.tint_b,
            (double)postfx_state.params.tint_intensity);
    fprintf(fp, "  Brightness: %.2f\n", (double)postfx_state.params.brightness);
    fprintf(fp, "  Contrast: %.2f\n", (double)postfx_state.params.contrast);
    fprintf(fp, "  Saturation: %.2f\n", (double)postfx_state.params.saturation);
}
