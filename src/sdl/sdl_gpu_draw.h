/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Simple Drawing
 *
 * Provides simple GPU-accelerated drawing for sprites and primitives.
 * This is a simpler alternative to the full batching system.
 */

#ifndef SDL_GPU_DRAW_H
#define SDL_GPU_DRAW_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

// Initialize simple GPU drawing
bool gpu_draw_init(int screen_width, int screen_height);

// Shutdown
void gpu_draw_shutdown(void);

// Resize screen
void gpu_draw_resize(int new_width, int new_height);

// Draw a textured quad
// texture: GPU texture to draw
// dest: Destination rectangle in screen pixels
// src: Source rectangle in texture pixels (or NULL for full texture)
// tex_width, tex_height: Texture dimensions (for UV calculation)
// color_mod: RGBA color modulation (or NULL for white)
// alpha: Alpha value (0-255)
void gpu_draw_texture(SDL_GPUTexture *texture,
                      const SDL_FRect *dest,
                      const SDL_FRect *src,
                      int tex_width, int tex_height,
                      const float *color_mod,
                      int alpha);

// Draw a filled rectangle
void gpu_draw_rect(float x, float y, float w, float h,
                   float r, float g, float b, float a);

// Check if simple drawing is available
bool gpu_draw_is_available(void);

// Check if primitive drawing is available
bool gpu_draw_prim_is_available(void);

// Check if line drawing is available
bool gpu_draw_line_is_available(void);

// Draw a line
void gpu_draw_line(float x1, float y1, float x2, float y2,
                   float r, float g, float b, float a);

#endif // SDL_GPU_DRAW_H
