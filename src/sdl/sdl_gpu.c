/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL GPU - Implementation
 *
 * SDL3 GPU API implementation providing hardware-accelerated rendering
 * with automatic fallback to SDL_Renderer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "sdl_gpu.h"
#include "sdl_gpu_post.h"
#include "sdl_gpu_batch.h"
#include "astonia.h"

// ============================================================================
// Global State
// ============================================================================

// GPU rendering mode flag
bool use_gpu_rendering = false;

// GPU device handle
SDL_GPUDevice *sdlgpu = NULL;

// Window reference (for swapchain operations)
static SDL_Window *gpu_window = NULL;

// Current frame state
static SDL_GPUCommandBuffer *current_cmd_buffer = NULL;
static SDL_GPUTexture *current_swapchain_texture = NULL;
static SDL_GPURenderPass *current_render_pass = NULL;
static bool using_postfx_this_frame = false;
static uint32_t current_swapchain_width = 0;
static uint32_t current_swapchain_height = 0;

// Debug counters
static int gpu_debug_frame_count = 0;
static int gpu_debug_draw_count = 0;

// Graphics pipelines
static SDL_GPUGraphicsPipeline *pipelines[GPU_PIPELINE_COUNT] = {NULL};

// Default sampler
static SDL_GPUSampler *default_sampler = NULL;

// ============================================================================
// Initialization and Shutdown
// ============================================================================

bool gpu_init(SDL_Window *window)
{
    if (!window) {
        note("gpu_init: NULL window provided");
        return false;
    }

    gpu_window = window;

    // Try to create GPU device with all supported shader formats
    sdlgpu = SDL_CreateGPUDevice(GPU_SHADER_FORMATS, false, NULL);

    if (!sdlgpu) {
        note("gpu_init: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        note("gpu_init: Falling back to SDL_Renderer");
        use_gpu_rendering = false;
        return false;
    }

    // Claim window for GPU rendering
    if (!SDL_ClaimWindowForGPUDevice(sdlgpu, window)) {
        note("gpu_init: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(sdlgpu);
        sdlgpu = NULL;
        use_gpu_rendering = false;
        return false;
    }

    // Create default sampler
    default_sampler = gpu_sampler_create();
    if (!default_sampler) {
        note("gpu_init: Failed to create default sampler");
        SDL_ReleaseWindowFromGPUDevice(sdlgpu, window);
        SDL_DestroyGPUDevice(sdlgpu);
        sdlgpu = NULL;
        use_gpu_rendering = false;
        return false;
    }

    use_gpu_rendering = true;
    note("gpu_init: GPU rendering enabled using %s", gpu_get_driver_name());

    return true;
}

void gpu_shutdown(void)
{
    if (!sdlgpu) {
        return;
    }

    // Wait for GPU to finish all work
    SDL_WaitForGPUIdle(sdlgpu);

    // Release pipelines
    gpu_pipelines_release();

    // Release default sampler
    if (default_sampler) {
        SDL_ReleaseGPUSampler(sdlgpu, default_sampler);
        default_sampler = NULL;
    }

    // Release window
    if (gpu_window) {
        SDL_ReleaseWindowFromGPUDevice(sdlgpu, gpu_window);
        gpu_window = NULL;
    }

    // Destroy device
    SDL_DestroyGPUDevice(sdlgpu);
    sdlgpu = NULL;
    use_gpu_rendering = false;

    note("gpu_shutdown: GPU rendering disabled");
}

bool gpu_is_active(void)
{
    return use_gpu_rendering && sdlgpu != NULL;
}

// ============================================================================
// Frame Management
// ============================================================================

bool gpu_frame_begin(void)
{
    if (!gpu_is_active()) {
        return false;
    }

    // Reset frame state
    using_postfx_this_frame = false;
    gpu_debug_draw_count = 0;

    // Acquire command buffer
    current_cmd_buffer = SDL_AcquireGPUCommandBuffer(sdlgpu);
    if (!current_cmd_buffer) {
        note("gpu_frame_begin: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return false;
    }

    // Wait for and acquire swapchain texture
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            current_cmd_buffer,
            gpu_window,
            &current_swapchain_texture,
            &current_swapchain_width,
            &current_swapchain_height)) {
        note("gpu_frame_begin: SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(current_cmd_buffer);
        current_cmd_buffer = NULL;
        return false;
    }

    if (!current_swapchain_texture) {
        // Window may be minimized, skip this frame
        SDL_CancelGPUCommandBuffer(current_cmd_buffer);
        current_cmd_buffer = NULL;
        return false;
    }

    // Try to use post-processing (renders to offscreen texture, then applies effects)
    if (gpu_postfx_is_enabled()) {
        current_render_pass = gpu_postfx_begin_scene(current_cmd_buffer);
        if (current_render_pass) {
            using_postfx_this_frame = true;
            // Set viewport for post-fx scene texture
            SDL_GPUViewport viewport = {
                .x = 0.0f, .y = 0.0f,
                .w = (float)current_swapchain_width,
                .h = (float)current_swapchain_height,
                .min_depth = 0.0f, .max_depth = 1.0f
            };
            SDL_SetGPUViewport(current_render_pass, &viewport);

            // Set batch context for sprite batching
            gpu_batch_set_context(current_cmd_buffer, current_render_pass);
            return true;
        }
        // Post-FX failed, fall through to direct swapchain rendering
    }

    // Direct swapchain rendering (fallback or post-fx not available)
    SDL_GPUColorTargetInfo color_target = {
        .texture = current_swapchain_texture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
        .resolve_texture = NULL,
        .resolve_mip_level = 0,
        .resolve_layer = 0,
        .cycle = false,
        .cycle_resolve_texture = false
    };

    current_render_pass = SDL_BeginGPURenderPass(current_cmd_buffer, &color_target, 1, NULL);
    if (!current_render_pass) {
        note("gpu_frame_begin: SDL_BeginGPURenderPass failed: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(current_cmd_buffer);
        current_cmd_buffer = NULL;
        current_swapchain_texture = NULL;
        return false;
    }

    // Set viewport for swapchain
    SDL_GPUViewport viewport = {
        .x = 0.0f, .y = 0.0f,
        .w = (float)current_swapchain_width,
        .h = (float)current_swapchain_height,
        .min_depth = 0.0f, .max_depth = 1.0f
    };
    SDL_SetGPUViewport(current_render_pass, &viewport);

    // Set batch context for sprite batching
    gpu_batch_set_context(current_cmd_buffer, current_render_pass);

    return true;
}

void gpu_frame_end(void)
{
    if (!current_cmd_buffer) {
        return;
    }

    // Flush any pending batched sprites before ending the render pass
    gpu_batch_flush();

    // End the main render pass
    if (current_render_pass) {
        if (using_postfx_this_frame) {
            // End scene render pass (renders to offscreen texture)
            gpu_postfx_end_scene(current_render_pass);
            current_render_pass = NULL;

            // Apply post-processing and render to swapchain
            gpu_postfx_present(current_cmd_buffer, current_swapchain_texture);
        } else {
            // Direct swapchain rendering - just end the pass
            SDL_EndGPURenderPass(current_render_pass);
            current_render_pass = NULL;
        }
    }

    // Submit command buffer (this also presents the swapchain)
    if (!SDL_SubmitGPUCommandBuffer(current_cmd_buffer)) {
        note("gpu_frame_end: SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
    }

    gpu_debug_frame_count++;
    current_cmd_buffer = NULL;
    current_swapchain_texture = NULL;
    using_postfx_this_frame = false;
}

SDL_GPUCommandBuffer *gpu_get_command_buffer(void)
{
    return current_cmd_buffer;
}

SDL_GPUTexture *gpu_get_swapchain_texture(void)
{
    return current_swapchain_texture;
}

SDL_GPURenderPass *gpu_get_render_pass(void)
{
    return current_render_pass;
}

void gpu_get_swapchain_size(int *width, int *height)
{
    if (width) *width = (int)current_swapchain_width;
    if (height) *height = (int)current_swapchain_height;
}

void gpu_debug_increment_draw_count(void)
{
    gpu_debug_draw_count++;
}

// ============================================================================
// Pipeline Management
// ============================================================================

bool gpu_pipelines_load(void)
{
    if (!gpu_is_active()) {
        return false;
    }

    // Pipelines will be created as shaders are loaded
    // For now, return success - actual loading happens in Phase 2
    note("gpu_pipelines_load: Pipeline loading deferred until shaders are available");
    return true;
}

void gpu_pipelines_release(void)
{
    if (!sdlgpu) {
        return;
    }

    for (int i = 0; i < GPU_PIPELINE_COUNT; i++) {
        if (pipelines[i]) {
            SDL_ReleaseGPUGraphicsPipeline(sdlgpu, pipelines[i]);
            pipelines[i] = NULL;
        }
    }
}

SDL_GPUGraphicsPipeline *gpu_get_pipeline(gpu_pipeline_id_t id)
{
    if (id < 0 || id >= GPU_PIPELINE_COUNT) {
        return NULL;
    }
    return pipelines[id];
}

// ============================================================================
// Texture Management
// ============================================================================

SDL_GPUTexture *gpu_texture_create(const uint32_t *pixels, int width, int height)
{
    if (!gpu_is_active() || !pixels || width <= 0 || height <= 0) {
        static int fail_log_count = 0;
        if (fail_log_count++ < 10) {
            note("gpu_texture_create: early fail - active=%d pixels=%d w=%d h=%d sdlgpu=%d use_gpu=%d",
                 gpu_is_active(), pixels != NULL, width, height, sdlgpu != NULL, use_gpu_rendering);
        }
        return NULL;
    }

    // Create texture
    SDL_GPUTextureCreateInfo tex_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = (Uint32)width,
        .height = (Uint32)height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1
    };

    SDL_GPUTexture *texture = SDL_CreateGPUTexture(sdlgpu, &tex_info);
    if (!texture) {
        note("gpu_texture_create: SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return NULL;
    }

    // Create transfer buffer for upload
    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32)((size_t)width * (size_t)height * sizeof(uint32_t))
    };

    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(sdlgpu, &transfer_info);
    if (!transfer) {
        note("gpu_texture_create: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(sdlgpu, texture);
        return NULL;
    }

    // Map and copy pixel data
    void *mapped = SDL_MapGPUTransferBuffer(sdlgpu, transfer, false);
    if (!mapped) {
        note("gpu_texture_create: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        SDL_ReleaseGPUTexture(sdlgpu, texture);
        return NULL;
    }

    memcpy(mapped, pixels, (size_t)width * (size_t)height * sizeof(uint32_t));
    SDL_UnmapGPUTransferBuffer(sdlgpu, transfer);

    // Upload to GPU
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
    if (!cmd) {
        note("gpu_texture_create: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        SDL_ReleaseGPUTexture(sdlgpu, texture);
        return NULL;
    }

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        note("gpu_texture_create: SDL_BeginGPUCopyPass failed: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
        SDL_ReleaseGPUTexture(sdlgpu, texture);
        return NULL;
    }

    SDL_GPUTextureTransferInfo src = {
        .transfer_buffer = transfer,
        .offset = 0,
        .pixels_per_row = (Uint32)width,
        .rows_per_layer = (Uint32)height
    };

    SDL_GPUTextureRegion dst = {
        .texture = texture,
        .mip_level = 0,
        .layer = 0,
        .x = 0,
        .y = 0,
        .z = 0,
        .w = (Uint32)width,
        .h = (Uint32)height,
        .d = 1
    };

    SDL_UploadToGPUTexture(copy_pass, &src, &dst, false);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_SubmitGPUCommandBuffer(cmd);

    // Release transfer buffer (texture data is now on GPU)
    SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);

    return texture;
}

void gpu_texture_destroy(SDL_GPUTexture *texture)
{
    if (texture && sdlgpu) {
        SDL_ReleaseGPUTexture(sdlgpu, texture);
    }
}

SDL_GPUSampler *gpu_sampler_create(void)
{
    // Note: Don't use gpu_is_active() here - this is called during init
    // before use_gpu_rendering is set to true
    if (!sdlgpu) {
        return NULL;
    }

    SDL_GPUSamplerCreateInfo sampler_info = {
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .mip_lod_bias = 0.0f,
        .max_anisotropy = 1.0f,
        .compare_op = SDL_GPU_COMPAREOP_NEVER,
        .min_lod = 0.0f,
        .max_lod = 1.0f,
        .enable_anisotropy = false,
        .enable_compare = false
    };

    return SDL_CreateGPUSampler(sdlgpu, &sampler_info);
}

// ============================================================================
// Render Targets
// ============================================================================

SDL_GPUTexture *gpu_render_target_create(int width, int height)
{
    if (!gpu_is_active() || width <= 0 || height <= 0) {
        return NULL;
    }

    SDL_GPUTextureCreateInfo tex_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = (Uint32)width,
        .height = (Uint32)height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1
    };

    SDL_GPUTexture *texture = SDL_CreateGPUTexture(sdlgpu, &tex_info);
    if (!texture) {
        note("gpu_render_target_create: SDL_CreateGPUTexture failed: %s", SDL_GetError());
    }

    return texture;
}

SDL_GPURenderPass *gpu_render_target_begin(SDL_GPUTexture *target, const SDL_FColor *clear_color)
{
    if (!current_cmd_buffer) {
        note("gpu_render_target_begin: No command buffer active");
        return NULL;
    }

    // Use swapchain if target is NULL
    SDL_GPUTexture *render_target = target ? target : current_swapchain_texture;
    if (!render_target) {
        note("gpu_render_target_begin: No render target available");
        return NULL;
    }

    SDL_GPUColorTargetInfo color_target = {
        .texture = render_target,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = clear_color ? *clear_color : (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f},
        .load_op = clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
        .resolve_texture = NULL,
        .resolve_mip_level = 0,
        .resolve_layer = 0,
        .cycle = false,
        .cycle_resolve_texture = false
    };

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(current_cmd_buffer, &color_target, 1, NULL);
    if (!pass) {
        note("gpu_render_target_begin: SDL_BeginGPURenderPass failed: %s", SDL_GetError());
    }

    return pass;
}

void gpu_render_target_end(SDL_GPURenderPass *pass)
{
    if (pass) {
        SDL_EndGPURenderPass(pass);
    }
}

// ============================================================================
// Debug and Diagnostics
// ============================================================================

const char *gpu_get_driver_name(void)
{
    if (!sdlgpu) {
        return "none";
    }
    return SDL_GetGPUDeviceDriver(sdlgpu);
}

void gpu_dump(FILE *fp)
{
    fprintf(fp, "GPU State:\n");
    fprintf(fp, "  use_gpu_rendering: %s\n", use_gpu_rendering ? "true" : "false");
    fprintf(fp, "  sdlgpu: %p\n", (void *)sdlgpu);
    fprintf(fp, "  driver: %s\n", gpu_get_driver_name());

    if (sdlgpu) {
        // Report supported shader formats
        fprintf(fp, "  shader_formats:");
        SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(sdlgpu);
        if (formats & SDL_GPU_SHADERFORMAT_SPIRV) fprintf(fp, " SPIRV");
        if (formats & SDL_GPU_SHADERFORMAT_DXBC) fprintf(fp, " DXBC");
        if (formats & SDL_GPU_SHADERFORMAT_DXIL) fprintf(fp, " DXIL");
        if (formats & SDL_GPU_SHADERFORMAT_MSL) fprintf(fp, " MSL");
        if (formats & SDL_GPU_SHADERFORMAT_METALLIB) fprintf(fp, " METALLIB");
        fprintf(fp, "\n");

        // Report pipeline status
        fprintf(fp, "  pipelines:\n");
        const char *pipeline_names[] = {"sprite", "primitive", "postfx"};
        for (int i = 0; i < GPU_PIPELINE_COUNT; i++) {
            fprintf(fp, "    %s: %s\n", pipeline_names[i], pipelines[i] ? "loaded" : "not loaded");
        }
    }

    fprintf(fp, "\n");
}
