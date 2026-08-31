/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Shader Effects - shared instance layout
 *
 * This header defines the per-sprite instance data consumed by the
 * sprite_fx shaders (res/shaders/sprite_fx.vert / sprite_fx.frag).
 * The shaders implement an EXACT port of the CPU effect pipeline
 * (sdl_effects.c + the bake loop in sdl_image.c:sdl_make); the client
 * uploads base sprite pixels once and applies effects per draw.
 *
 * The struct layout must match the std430 InstanceBuffer struct in
 * sprite_fx.vert/.frag exactly (128 bytes).
 *
 * tests/test_shaderfx_compare.c renders sprites through both paths and
 * diffs the pixels; change either side only together with the checker.
 */

#ifndef SDL_GPU_SHADERFX_H
#define SDL_GPU_SHADERFX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Effect-instance flags (colorize[3]) */
#define GPU_FX_COLORIZE_NEW (1u << 0) /* sprite >= 220000: sdl_colorize_pix2 algorithm */

/* Per-sprite instance data, std430-compatible, 128 bytes.
 * All effect parameters are the RAW values the CPU pipeline receives
 * (struct sdl_texture fields) - scaling/precision quirks are replicated
 * inside the shader, not here. */
typedef struct gpu_fx_instance {
	/* Destination rectangle in device pixels (post sdl_scale). */
	float dest[4]; /* x, y, w, h */
	/* Source region in page texels (matches dest w/h 1:1). */
	int32_t src[4]; /* x, y, w, h */
	/* Sprite placement inside the page: origin texel and full sprite
	 * size in texels (xres*sdl_scale, yres*sdl_scale). The lighting
	 * geometry and the sink cutoff work in sprite-relative texels, so
	 * clipped draws still light correctly. */
	int32_t org_sz[4]; /* org.x, org.y, size.x, size.y */
	/* Colorize channels c1/c2/c3 (raw 16-bit values including the
	 * 0x8000 embedded-shine bit); w = GPU_FX_* flags. */
	uint32_t colorize[4];
	/* Color balance cr/cg/cb (raw signed values, pre-typo-scaling) and
	 * lightness. */
	int32_t balance[4];
	/* x=sat (0..20), y=shine (0..100), z=freeze (0..7),
	 * w=sink in device pixels (min-clamped like sdl_make). */
	int32_t fx[4];
	/* Directional light levels 0..15: ml, ll, rl, ul. */
	int32_t light_a[4];
	/* x=dl, y=draw alpha 0..255 (render_sprite_fx fx->alpha; 255=opaque),
	 * z/w reserved. */
	int32_t light_b[4];
} gpu_fx_instance_t;

/* Per-frame/per-flush uniforms. Must match the shaders' std140 blocks. */
typedef struct gpu_fx_vs_uniforms {
	float screen_w, screen_h; /* viewport size in device pixels */
	float inv_screen_w, inv_screen_h;
	uint32_t base_instance; /* first instance of this draw in the buffer */
	uint32_t pad0, pad1, pad2;
} gpu_fx_vs_uniforms_t;

typedef struct gpu_fx_ps_uniforms {
	int32_t sdl_scale; /* the global sdl_scale */
	int32_t le_bonus; /* +8 GO_LIGHTER, +12 GO_LIGHTER2 (cumulative) */
	uint32_t base_instance;
	int32_t pad0;
} gpu_fx_ps_uniforms_t;

#ifdef __cplusplus
}
#endif

#endif /* SDL_GPU_SHADERFX_H */
