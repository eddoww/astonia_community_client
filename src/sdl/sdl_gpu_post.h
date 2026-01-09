/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Post-Processing System
 *
 * This module provides GPU-accelerated post-processing effects using SDL3 GPU API.
 * Effects include:
 * - Vignette (radial edge darkening)
 * - Screen tint (color overlay for damage, effects, etc.)
 * - Brightness/Contrast/Saturation adjustments
 *
 * These effects run in a single draw call using a fullscreen shader pass,
 * replacing the CPU-based multi-rect approach for better performance.
 */

#ifndef SDL_GPU_POST_H
#define SDL_GPU_POST_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

// ============================================================================
// Post-Processing Parameters
// ============================================================================

// Uniform buffer structure (must match postfx.hlsl layout)
typedef struct gpu_postfx_params {
    float screen_width;           // Screen dimensions in pixels
    float screen_height;
    float vignette_intensity;     // Vignette strength (0 = off, 1 = max)
    float vignette_radius;        // Vignette inner radius (0.3 = default)

    float tint_r;                 // Screen tint RGB (0-1)
    float tint_g;
    float tint_b;
    float tint_intensity;         // Tint strength (0 = off, 1 = full)

    float brightness;             // Brightness adjustment (-1 to 1)
    float contrast;               // Contrast adjustment (0.5 to 2.0)
    float saturation;             // Saturation (0 = grayscale, 1 = normal, 2 = vivid)
    float padding;                // Padding for 16-byte alignment
} gpu_postfx_params_t;

// ============================================================================
// Post-Processing State
// ============================================================================

typedef struct gpu_postfx_state {
    // Pipeline and shaders
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;

    // Scene render target (render game to this, then post-process to swapchain)
    SDL_GPUTexture *scene_texture;
    int scene_width;
    int scene_height;

    // Fullscreen quad vertex buffer
    SDL_GPUBuffer *quad_vbo;

    // Uniform buffer for post-fx parameters
    gpu_postfx_params_t params;

    // Sampler for scene texture
    SDL_GPUSampler *sampler;

    // State tracking
    bool initialized;
    bool enabled;
} gpu_postfx_state_t;

// ============================================================================
// API Functions
// ============================================================================

// Initialize post-processing system
// Call after gpu_init() succeeds
// Returns true on success, false if GPU not available or shader loading fails
bool gpu_postfx_init(int screen_width, int screen_height);

// Shutdown post-processing system
void gpu_postfx_shutdown(void);

// Resize the scene render target (call on window resize)
bool gpu_postfx_resize(int new_width, int new_height);

// Begin rendering scene to offscreen target
// Returns the render pass to draw the scene into, or NULL on failure
// The caller should draw all game content using this pass
SDL_GPURenderPass *gpu_postfx_begin_scene(SDL_GPUCommandBuffer *cmd);

// End scene rendering pass
void gpu_postfx_end_scene(SDL_GPURenderPass *pass);

// Apply post-processing and present to swapchain
// This performs the fullscreen shader pass with all enabled effects
// cmd: Command buffer to use
// swapchain_texture: The swapchain texture to render to
void gpu_postfx_present(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *swapchain_texture);

// ============================================================================
// Effect Configuration
// ============================================================================

// Enable/disable entire post-processing pipeline
void gpu_postfx_set_enabled(bool enabled);
bool gpu_postfx_is_enabled(void);

// Vignette effect
void gpu_postfx_set_vignette(float intensity, float radius);
void gpu_postfx_clear_vignette(void);

// Screen tint (color overlay)
// color: 16-bit color value (RRRRRGGGGGGBBBBB format, matches existing code)
// intensity: 0-255
void gpu_postfx_set_tint(uint16_t color, uint8_t intensity);
void gpu_postfx_set_tint_rgb(float r, float g, float b, float intensity);
void gpu_postfx_clear_tint(void);

// Color adjustments
void gpu_postfx_set_brightness(float brightness);  // -1 to 1
void gpu_postfx_set_contrast(float contrast);      // 0.5 to 2.0
void gpu_postfx_set_saturation(float saturation);  // 0 to 2

// Reset all effects to defaults
void gpu_postfx_reset(void);

// ============================================================================
// Utility
// ============================================================================

// Get the scene texture for direct access (e.g., for debugging or custom effects)
SDL_GPUTexture *gpu_postfx_get_scene_texture(void);

// Dump state for debugging
void gpu_postfx_dump(FILE *fp);

#endif // SDL_GPU_POST_H
