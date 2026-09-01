/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Glow - batched additive capsule glows for spell effects
 *
 * The effect code has always drawn its glows by hand: sdl_pretty_pixel()
 * stacks concentric alpha rings around a point, sdl_rain_pixel() does the
 * same for a droplet, and render_display_strike() draws nine offset lines
 * whose colour ramps down from the centre. All three are falloffs written
 * out by hand because the SDL_Renderer path had no cheaper way to do it,
 * and all three are stepped, hard-edged and capped at three device pixels.
 *
 * This module draws the same shapes as real radial falloffs instead
 * (res/shaders/glow.*). One instance is a CAPSULE - a segment plus a
 * radius - so a sparkle (p0 == p1) and a lightning bolt (p0 != p1) share
 * one pipeline and one batch.
 *
 * Batching mirrors sdl_gpu_shaderfx.c: a ring of instance/transfer
 * buffers, no mid-frame fence waits, one upload command buffer submitted
 * before the render command buffer, and a base-instance uniform so draws
 * can reference a sub-range.
 *
 * Blending is SRC_ALPHA/ONE (classic particle additive, the same state
 * sdl_set_blend_mode() calls ADD), which is what makes these read as
 * light rather than paint.
 */

#ifndef SDL_GPU_GLOW_H
#define SDL_GPU_GLOW_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-glow instance, std430-compatible, 48 bytes. Coordinates are device
 * pixels (post sdl_scale), matching gpu_draw_rect()/gpu_draw_line(). */
typedef struct gpu_glow_instance {
	float seg[4]; /* p0.x, p0.y, p1.x, p1.y (p0 == p1 for a point) */
	float shape[4]; /* radius, core weight, unused, unused */
	float color[4]; /* r, g, b, intensity (all 0..1) */
} gpu_glow_instance_t;

/* Vertex uniforms; must match the std140 block in glow.vert. */
typedef struct gpu_glow_vs_uniforms {
	float inv_screen_w, inv_screen_h;
	uint32_t base_instance;
	uint32_t pad0;
} gpu_glow_vs_uniforms_t;

/* ====================================================================
 * lifecycle (src/sdl/sdl_gpu_glow.c)
 * ==================================================================== */

/* Build the pipeline and instance ring. Safe to call when the GPU path
 * is up; returns false (and leaves the module inert) otherwise. Unlike
 * the shader-effects path this is NOT gated on a launch flag - the
 * gpu_fancy_effects option is checked per draw, so it can be toggled at
 * runtime without a restart. */
bool gpu_glow_init(void);
void gpu_glow_shutdown(void);

/* True once the pipeline is usable. Does not consider the user option -
 * use sdl_fancy_effects_active() for that. */
bool gpu_glow_ready(void);

void gpu_glow_frame_begin(void);
void gpu_glow_flush(void);
void gpu_glow_submit_upload(void);

/* Flush the pending run so an interleaved non-batched draw keeps its
 * painter order relative to batched glows. */
void gpu_glow_direct_draw_barrier(void);

void gpu_glow_get_stats(int *draws, int *glows);

/* ====================================================================
 * draw submission
 * ==================================================================== */

/* Add one capsule glow. Coordinates and radius are device pixels; rgb
 * and intensity are 0..1. `core` weights the tight centre lobe (0 = pure
 * soft halo, ~1.5 = sparkle with a hot middle). Returns false when the
 * batch cannot take it (no frame, capacity reached), so callers can fall
 * back to the plain primitives. */
bool gpu_glow_add(
    float x0, float y0, float x1, float y1, float radius, float core, float r, float g, float b, float intensity);

#ifdef __cplusplus
}
#endif

#endif // SDL_GPU_GLOW_H
