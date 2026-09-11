/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Primitive Batch
 *
 * gpu_draw_rect() is the workhorse of the primitive path, and it used to
 * cost a pipeline bind, a vertex-buffer bind, a uniform push and a draw
 * call PER RECTANGLE. That matters because almost nothing in the client
 * draws a single rectangle: circles and rings walk a midpoint sweep, arcs
 * plot up to 361 points, anti-aliased lines plot one point per pixel of
 * the line, and every one of those points is a 1x1 rect. A single UI arc
 * was 361 draw calls; the AA lines a mod draws for a beam were one per
 * pixel.
 *
 * This module batches them into an instance buffer instead, so a whole
 * circle, arc or line goes out as one draw. Same model as
 * sdl_gpu_shaderfx.c and sdl_gpu_glow.c: a ring of instance/transfer
 * buffers, no mid-frame fence waits, one upload command buffer submitted
 * before the render command buffer, and a base-instance uniform so a draw
 * can reference a sub-range.
 *
 * Blend mode is baked into the pipeline, so the batch also flushes when
 * sdl_set_blend_mode() changes it - see gpu_prim_batch_add().
 */

#ifndef SDL_GPU_PRIM_H
#define SDL_GPU_PRIM_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-rect instance, std430-compatible, 32 bytes. Device pixels. */
typedef struct gpu_prim_instance {
	float dest[4]; /* x, y, w, h */
	float color[4]; /* r, g, b, a (0..1) */
} gpu_prim_instance_t;

/* Vertex uniforms; must match the std140 block in prim_batch.vert. */
typedef struct gpu_prim_vs_uniforms {
	float inv_screen_w, inv_screen_h;
	uint32_t base_instance;
	uint32_t pad0;
} gpu_prim_vs_uniforms_t;

bool gpu_prim_batch_init(void);
void gpu_prim_batch_shutdown(void);
bool gpu_prim_batch_ready(void);

void gpu_prim_batch_frame_begin(void);
void gpu_prim_batch_flush(void);
void gpu_prim_batch_submit_upload(void);

/* Flush the pending run so an interleaved draw of any other kind keeps
 * its painter order relative to batched rects. */
void gpu_prim_batch_direct_draw_barrier(void);

void gpu_prim_batch_get_stats(int *draws, int *rects);

/* Queue one rectangle. Returns false when the batch cannot take it (no
 * frame, capacity reached), so gpu_draw_rect() can fall back to its
 * unbatched draw. */
bool gpu_prim_batch_add(float x, float y, float w, float h, float r, float g, float b, float a);

#ifdef __cplusplus
}
#endif

#endif // SDL_GPU_PRIM_H
