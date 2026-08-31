/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Base-Texture Atlas (shader-effects path)
 *
 * Packs BASE sprite textures (effect-free, one per sprite - see
 * sdl_gpu_shaderfx.c) into large shared pages so consecutive sprite
 * draws hit the same texture and the instanced batcher can merge them
 * into few draw calls. Pixel exactness is preserved because the fx
 * shader addresses texels with texelFetch (no filtering, no bleed).
 *
 * Allocation is a simple shelf packer with height-quantized shelves.
 * Regions are NOT reclaimed when a cache entry is evicted yet (the
 * base-texture working set is small and stable; a free list is planned
 * for phase 2) - when the pages fill up, gpu_atlas_insert returns NULL
 * and the caller falls back to a standalone texture.
 */

#ifndef SDL_GPU_ATLAS_H
#define SDL_GPU_ATLAS_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPU_ATLAS_PAGE_SIZE 2048
#define GPU_ATLAS_MAX_PAGES 16

/* Insert a w x h ARGB pixel block. On success returns the page texture
 * and writes the region origin to out_x/out_y. Returns NULL when the
 * block is too large, the pages are full, or the upload failed - the
 * caller should create a standalone texture instead. Thread-safe. */
SDL_GPUTexture *gpu_atlas_insert(const uint32_t *pixels, int w, int h, int *out_x, int *out_y);

/* Release all pages (client shutdown). */
void gpu_atlas_shutdown(void);

/* Diagnostics: pages in use and total texels allocated. */
void gpu_atlas_get_stats(int *pages, long long *used_texels);

#ifdef __cplusplus
}
#endif

#endif /* SDL_GPU_ATLAS_H */
