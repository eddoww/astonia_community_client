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

// Number of blend-mode pipeline variants (indices mirror sdl_set_blend_mode:
// 0=BLEND 1=ADD 2=MOD 3=MUL 4=NONE)
#define GPU_DRAW_BLEND_MODES 5

// Initialize simple GPU drawing
bool gpu_draw_init(int screen_width, int screen_height);

// Select the blend mode used by subsequent sprite/primitive/line draws
// (mode indices as above; out-of-range values fall back to 0/BLEND)
void gpu_draw_set_blend_mode(int mode);
int gpu_draw_get_blend_mode(void);

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
void gpu_draw_texture(SDL_GPUTexture *texture, const SDL_FRect *dest, const SDL_FRect *src, int tex_width,
    int tex_height, const float *color_mod, int alpha);

// Draw a textured quad rotated 45 degrees clockwise around the center of
// `dest` (GPU counterpart of SDL_RenderTextureRotated(..., 45.0, NULL,
// SDL_FLIP_NONE), used by the round minimap). Only valid for square dest
// rects - rotation and non-uniform scaling don't commute. Same parameter
// semantics as gpu_draw_texture otherwise.
void gpu_draw_texture_rot45(SDL_GPUTexture *texture, const SDL_FRect *dest, const SDL_FRect *src, int tex_width,
    int tex_height, const float *color_mod, int alpha);

// Draw a filled rectangle
void gpu_draw_rect(float x, float y, float w, float h, float r, float g, float b, float a);

// Check if simple drawing is available
bool gpu_draw_is_available(void);

// Check if primitive drawing is available
bool gpu_draw_prim_is_available(void);

// Check if line drawing is available
bool gpu_draw_line_is_available(void);

// Draw a line
void gpu_draw_line(float x1, float y1, float x2, float y2, float r, float g, float b, float a);

// Check if triangle drawing is available
bool gpu_draw_tri_is_available(void);

// Draw a triangle with per-vertex colors (each color is float[4] RGBA 0..1).
// c1/c2 may be NULL to reuse c0 (solid triangle). Coordinates are in screen
// pixels of the currently bound target (like gpu_draw_rect/gpu_draw_line).
void gpu_draw_triangle(float x0, float y0, float x1, float y1, float x2, float y2, const float c0[4], const float c1[4],
    const float c2[4]);

#endif // SDL_GPU_DRAW_H
