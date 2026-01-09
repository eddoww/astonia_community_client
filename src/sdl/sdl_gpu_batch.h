/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Sprite Batching System
 *
 * This module provides efficient batched sprite rendering using SDL3 GPU API.
 * Features:
 * - Instance buffer for per-sprite data (transforms, effects, colors)
 * - Automatic batch management (flush on texture change or buffer full)
 * - GPU-accelerated effects via sprite shader
 *
 * Sprites are batched by texture - when the texture changes, the current batch
 * is flushed before starting a new one.
 */

#ifndef SDL_GPU_BATCH_H
#define SDL_GPU_BATCH_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

// ============================================================================
// Configuration
// ============================================================================

// Maximum sprites per batch (must match shader instance buffer size)
#define GPU_BATCH_MAX_SPRITES 4096

// ============================================================================
// Sprite Instance Structure
// ============================================================================

// Per-sprite instance data (matches sprite_batch.vert SpriteInstance)
// Total size: 128 bytes (8 groups × 16 bytes, aligned to 16 bytes)
typedef struct gpu_sprite_instance {
    // Position and size (16 bytes)
    float dest_x, dest_y;         // Screen position
    float dest_w, dest_h;         // Screen size

    // Texture coordinates (16 bytes)
    float src_u, src_v;           // UV origin
    float src_w, src_h;           // UV size

    // Color modulation (16 bytes)
    float color_r, color_g, color_b, color_a;

    // Effect parameters (16 bytes)
    float light;                  // Base light level (0-16)
    float freeze;                 // Freeze effect (0-7)
    float shine;                  // Shine effect (0-100)
    float alpha;                  // Alpha (0-255)

    // Directional lighting (16 bytes)
    float ml, ll, rl, ul;         // Middle, left, right, up light

    // Additional parameters (16 bytes)
    float dl;                     // Down light
    float c1, c2, c3;             // Colorization channels

    // Color balance (16 bytes)
    float cr, cg, cb, light_adj;  // Color balance RGB + lightness

    // Saturation and flags (16 bytes, padded)
    float saturation;             // Saturation adjustment
    uint32_t flags;               // Effect flags
    float padding[2];             // Alignment padding
} gpu_sprite_instance_t;

// Effect flags (matches sprite.hlsl)
#define GPU_EFFECT_COLORIZE      (1 << 0)
#define GPU_EFFECT_COLOR_BALANCE (1 << 1)
#define GPU_EFFECT_FREEZE        (1 << 2)
#define GPU_EFFECT_SHINE         (1 << 3)
#define GPU_EFFECT_LIGHTING      (1 << 4)
#define GPU_EFFECT_SINK          (1 << 5)

// ============================================================================
// Batch State
// ============================================================================

typedef struct gpu_batch_state {
    // Pipeline and shaders
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;

    // Vertex buffer for quad (shared across all batches)
    SDL_GPUBuffer *quad_vbo;

    // Instance buffer (uploaded each frame)
    SDL_GPUBuffer *instance_buffer;
    SDL_GPUTransferBuffer *instance_transfer;

    // Staging buffer (CPU side)
    gpu_sprite_instance_t *instances;
    int instance_count;

    // Current texture being batched
    SDL_GPUTexture *current_texture;
    SDL_GPUSampler *sampler;

    // Uniform buffer for frame data
    float screen_width;
    float screen_height;

    // Statistics
    int batches_this_frame;
    int sprites_this_frame;

    // State
    bool initialized;
    bool in_batch;  // Currently batching (between begin/end)
} gpu_batch_state_t;

// ============================================================================
// API Functions
// ============================================================================

// Initialize batching system
// Call after gpu_init() succeeds
bool gpu_batch_init(int screen_width, int screen_height);

// Shutdown batching system
void gpu_batch_shutdown(void);

// Resize screen dimensions (call on window resize)
void gpu_batch_resize(int new_width, int new_height);

// Begin a new frame of batched rendering
// Returns render pass for drawing, or NULL on failure
// Pass should be used for all batched sprite draws
SDL_GPURenderPass *gpu_batch_begin_frame(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target);

// End the current frame
// Flushes any remaining batched sprites
void gpu_batch_end_frame(void);

// ============================================================================
// Sprite Submission
// ============================================================================

// Add a sprite to the current batch
// If texture differs from current batch, flushes previous batch first
// Parameters:
//   texture: GPU texture to draw
//   dest: Destination rectangle in screen coordinates
//   src: Source rectangle in UV coordinates (0-1)
//   instance: Per-sprite instance data (effects, colors, etc.)
void gpu_batch_add_sprite(SDL_GPUTexture *texture,
                          const SDL_FRect *dest,
                          const SDL_FRect *src,
                          const gpu_sprite_instance_t *instance);

// Simplified sprite add (no effects)
// For sprites that don't need GPU effects
void gpu_batch_add_sprite_simple(SDL_GPUTexture *texture,
                                  const SDL_FRect *dest,
                                  const SDL_FRect *src,
                                  float alpha);

// Force flush current batch (e.g., before non-batched draw calls)
void gpu_batch_flush(void);

// Set render context from external GPU system
// This allows the batch system to use an existing render pass
void gpu_batch_set_context(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass);

// Check if batch pipeline is ready
bool gpu_batch_is_available(void);

// ============================================================================
// Statistics & Debug
// ============================================================================

// Get statistics from last frame
int gpu_batch_get_batch_count(void);
int gpu_batch_get_sprite_count(void);

// Dump state for debugging
void gpu_batch_dump(FILE *fp);

#endif // SDL_GPU_BATCH_H
