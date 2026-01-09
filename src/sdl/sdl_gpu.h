/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL GPU - Header
 *
 * SDL3 GPU API abstraction layer for hardware-accelerated rendering with
 * shader support. Provides automatic fallback to SDL_Renderer for systems
 * without GPU support.
 */

#ifndef SDL_GPU_H
#define SDL_GPU_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// GPU State
// ============================================================================

// Global GPU rendering mode flag
// When true, use SDL_GPU path; when false, use SDL_Renderer fallback
extern bool use_gpu_rendering;

// SDL GPU device handle (NULL if GPU rendering not available)
extern SDL_GPUDevice *sdlgpu;

// ============================================================================
// Shader Format Flags
// ============================================================================

// Supported shader formats for cross-platform compatibility
#define GPU_SHADER_FORMATS (SDL_GPU_SHADERFORMAT_SPIRV | \
                            SDL_GPU_SHADERFORMAT_DXIL | \
                            SDL_GPU_SHADERFORMAT_MSL)

// ============================================================================
// Initialization and Shutdown
// ============================================================================

/**
 * Initialize the GPU rendering system.
 *
 * Attempts to create an SDL GPU device and claim the window. If successful,
 * sets use_gpu_rendering = true. If GPU initialization fails, the system
 * falls back to SDL_Renderer (use_gpu_rendering = false).
 *
 * @param window The SDL window to use for GPU rendering
 * @return true if GPU rendering is available, false if using fallback
 */
bool gpu_init(SDL_Window *window);

/**
 * Shutdown the GPU rendering system.
 *
 * Releases all GPU resources including pipelines, shaders, buffers, and
 * the GPU device itself. Safe to call even if GPU was not initialized.
 */
void gpu_shutdown(void);

/**
 * Check if GPU rendering is currently active.
 *
 * @return true if GPU rendering is being used, false if using SDL_Renderer
 */
bool gpu_is_active(void);

// ============================================================================
// Frame Management
// ============================================================================

/**
 * Begin a new GPU frame.
 *
 * Acquires a command buffer and the swapchain texture. Must be called at
 * the start of each frame when use_gpu_rendering is true.
 *
 * @return true on success, false if frame acquisition failed
 */
bool gpu_frame_begin(void);

/**
 * End the current GPU frame.
 *
 * Submits the command buffer and presents the swapchain. Must be called
 * at the end of each frame when gpu_frame_begin() returned true.
 */
void gpu_frame_end(void);

/**
 * Get the current command buffer.
 *
 * Only valid between gpu_frame_begin() and gpu_frame_end().
 *
 * @return The current command buffer, or NULL if not in a frame
 */
SDL_GPUCommandBuffer *gpu_get_command_buffer(void);

/**
 * Get the current swapchain texture.
 *
 * Only valid between gpu_frame_begin() and gpu_frame_end().
 *
 * @return The current swapchain texture, or NULL if not in a frame
 */
SDL_GPUTexture *gpu_get_swapchain_texture(void);

/**
 * Get the current render pass.
 *
 * Only valid between gpu_frame_begin() and gpu_frame_end().
 * This is the main scene render pass that targets the swapchain.
 *
 * @return The current render pass, or NULL if not in a frame
 */
SDL_GPURenderPass *gpu_get_render_pass(void);

/**
 * Get the current swapchain dimensions.
 *
 * Only valid between gpu_frame_begin() and gpu_frame_end().
 *
 * @param width Output for width, or NULL to ignore
 * @param height Output for height, or NULL to ignore
 */
void gpu_get_swapchain_size(int *width, int *height);

/**
 * Increment the draw call counter (for debugging).
 */
void gpu_debug_increment_draw_count(void);

// ============================================================================
// Pipeline Management
// ============================================================================

// Pipeline identifiers
typedef enum {
    GPU_PIPELINE_SPRITE = 0,      // Main sprite rendering pipeline
    GPU_PIPELINE_PRIMITIVE,       // Simple colored shapes (lines, rects)
    GPU_PIPELINE_POSTFX,          // Post-processing effects
    GPU_PIPELINE_COUNT
} gpu_pipeline_id_t;

/**
 * Load and compile all shader pipelines.
 *
 * Loads shader bytecode from the appropriate format for the current platform
 * and creates graphics pipelines. Must be called after gpu_init().
 *
 * @return true if all pipelines loaded successfully, false on error
 */
bool gpu_pipelines_load(void);

/**
 * Release all shader pipelines.
 *
 * Called automatically by gpu_shutdown().
 */
void gpu_pipelines_release(void);

/**
 * Get a graphics pipeline by ID.
 *
 * @param id The pipeline identifier
 * @return The graphics pipeline, or NULL if not loaded
 */
SDL_GPUGraphicsPipeline *gpu_get_pipeline(gpu_pipeline_id_t id);

// ============================================================================
// Texture Management
// ============================================================================

/**
 * Create a GPU texture from pixel data.
 *
 * @param pixels ARGB8888 pixel data
 * @param width Texture width in pixels
 * @param height Texture height in pixels
 * @return GPU texture handle, or NULL on failure
 */
SDL_GPUTexture *gpu_texture_create(const uint32_t *pixels, int width, int height);

/**
 * Destroy a GPU texture.
 *
 * @param texture The texture to destroy (safe to pass NULL)
 */
void gpu_texture_destroy(SDL_GPUTexture *texture);

/**
 * Create a GPU sampler with standard settings.
 *
 * Creates a linear filtering sampler with clamp-to-edge addressing.
 *
 * @return The sampler, or NULL on failure
 */
SDL_GPUSampler *gpu_sampler_create(void);

// ============================================================================
// Render Targets
// ============================================================================

/**
 * Create a render target texture.
 *
 * @param width Target width in pixels
 * @param height Target height in pixels
 * @return Render target texture, or NULL on failure
 */
SDL_GPUTexture *gpu_render_target_create(int width, int height);

/**
 * Begin rendering to a render target.
 *
 * @param target The render target (NULL for swapchain)
 * @param clear_color Color to clear the target with (NULL to skip clear)
 * @return Render pass handle, or NULL on failure
 */
SDL_GPURenderPass *gpu_render_target_begin(SDL_GPUTexture *target, const SDL_FColor *clear_color);

/**
 * End rendering to the current render target.
 *
 * @param pass The render pass to end
 */
void gpu_render_target_end(SDL_GPURenderPass *pass);

// ============================================================================
// Debug and Diagnostics
// ============================================================================

/**
 * Get the name of the GPU driver being used.
 *
 * @return Driver name string, or "none" if GPU not active
 */
const char *gpu_get_driver_name(void);

/**
 * Dump GPU state information to a file.
 *
 * @param fp File to write to
 */
void gpu_dump(FILE *fp);

#ifdef __cplusplus
}
#endif

#endif // SDL_GPU_H
