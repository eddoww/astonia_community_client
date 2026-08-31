/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Base-Texture Atlas - see sdl_gpu_atlas.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "astonia.h"
#include "sdl/sdl.h"
#include "sdl/sdl_gpu.h"
#include "sdl/sdl_gpu_atlas.h"

#define MAX_SHELVES_PER_PAGE 128
/* sprites larger than this stay standalone textures */
#define MAX_ATLAS_DIM 512
/* shelves come in heights quantized to multiples of this */
#define SHELF_QUANT 8

typedef struct atlas_shelf {
	int y; /* top of the shelf in the page */
	int h; /* quantized shelf height */
	int next_x; /* next free x position */
} atlas_shelf_t;

typedef struct atlas_page {
	SDL_GPUTexture *texture;
	atlas_shelf_t shelves[MAX_SHELVES_PER_PAGE];
	int num_shelves;
	int next_shelf_y; /* top of the unused area below the shelves */
} atlas_page_t;

static struct {
	atlas_page_t pages[GPU_ATLAS_MAX_PAGES];
	int num_pages;
	long long used_texels;
	SDL_Mutex *mutex;
	bool mutex_failed;
} atlas = {0};

static SDL_GPUTexture *atlas_create_page_texture(void)
{
	SDL_GPUTextureCreateInfo info = {
	    .type = SDL_GPU_TEXTURETYPE_2D,
	    .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
	    .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
	    .width = GPU_ATLAS_PAGE_SIZE,
	    .height = GPU_ATLAS_PAGE_SIZE,
	    .layer_count_or_depth = 1,
	    .num_levels = 1,
	    .sample_count = SDL_GPU_SAMPLECOUNT_1,
	};
	SDL_GPUTexture *tex = SDL_CreateGPUTexture(sdlgpu, &info);
	if (!tex) {
		note("gpu_atlas: page texture create failed: %s", SDL_GetError());
	}
	return tex;
}

/* Reserve a w x h region. Returns the page index or -1; writes origin. */
static int atlas_reserve(int w, int h, int *out_x, int *out_y)
{
	int hq = (h + SHELF_QUANT - 1) & ~(SHELF_QUANT - 1);

	for (int p = 0; p < atlas.num_pages; p++) {
		atlas_page_t *page = &atlas.pages[p];

		/* existing shelf with matching height class and room? */
		for (int s = 0; s < page->num_shelves; s++) {
			atlas_shelf_t *shelf = &page->shelves[s];
			if (shelf->h == hq && shelf->next_x + w <= GPU_ATLAS_PAGE_SIZE) {
				*out_x = shelf->next_x;
				*out_y = shelf->y;
				shelf->next_x += w;
				return p;
			}
		}

		/* open a new shelf on this page? */
		if (page->num_shelves < MAX_SHELVES_PER_PAGE && page->next_shelf_y + hq <= GPU_ATLAS_PAGE_SIZE) {
			atlas_shelf_t *shelf = &page->shelves[page->num_shelves++];
			shelf->y = page->next_shelf_y;
			shelf->h = hq;
			shelf->next_x = w;
			page->next_shelf_y += hq;
			*out_x = 0;
			*out_y = shelf->y;
			return p;
		}
	}

	/* new page? */
	if (atlas.num_pages < GPU_ATLAS_MAX_PAGES) {
		SDL_GPUTexture *tex = atlas_create_page_texture();
		if (!tex) {
			return -1;
		}
		atlas_page_t *page = &atlas.pages[atlas.num_pages];
		memset(page, 0, sizeof(*page));
		page->texture = tex;
		page->num_shelves = 1;
		page->shelves[0].y = 0;
		page->shelves[0].h = hq;
		page->shelves[0].next_x = w;
		page->next_shelf_y = hq;
		atlas.num_pages++;
		note("gpu_atlas: opened page %d (%dx%d)", atlas.num_pages - 1, GPU_ATLAS_PAGE_SIZE, GPU_ATLAS_PAGE_SIZE);
		*out_x = 0;
		*out_y = 0;
		return atlas.num_pages - 1;
	}

	return -1;
}

static bool atlas_upload_region(SDL_GPUTexture *tex, const uint32_t *pixels, int x, int y, int w, int h)
{
	size_t size = (size_t)w * (size_t)h * sizeof(uint32_t);

	SDL_GPUTransferBufferCreateInfo ti = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = (Uint32)size};
	SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(sdlgpu, &ti);
	if (!tb) {
		return false;
	}
	void *m = SDL_MapGPUTransferBuffer(sdlgpu, tb, false);
	if (!m) {
		SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
		return false;
	}
	memcpy(m, pixels, size);
	SDL_UnmapGPUTransferBuffer(sdlgpu, tb);

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
	if (!cmd) {
		SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
		return false;
	}
	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	if (!cp) {
		SDL_CancelGPUCommandBuffer(cmd);
		SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
		return false;
	}
	SDL_GPUTextureTransferInfo tti = {
	    .transfer_buffer = tb, .offset = 0, .pixels_per_row = (Uint32)w, .rows_per_layer = (Uint32)h};
	SDL_GPUTextureRegion reg = {.texture = tex, .x = (Uint32)x, .y = (Uint32)y, .w = (Uint32)w, .h = (Uint32)h, .d = 1};
	SDL_UploadToGPUTexture(cp, &tti, &reg, false);
	SDL_EndGPUCopyPass(cp);
	SDL_SubmitGPUCommandBuffer(cmd);
	SDL_ReleaseGPUTransferBuffer(sdlgpu, tb);
	return true;
}

SDL_GPUTexture *gpu_atlas_insert(const uint32_t *pixels, int w, int h, int *out_x, int *out_y)
{
	if (!sdlgpu || !pixels || w <= 0 || h <= 0 || w > MAX_ATLAS_DIM || h > MAX_ATLAS_DIM) {
		return NULL;
	}

	/* lazily created, permanent mutex (concurrent texture-stage callers) */
	if (!atlas.mutex && !atlas.mutex_failed) {
		SDL_Mutex *mtx = SDL_CreateMutex();
		if (mtx) {
			atlas.mutex = mtx;
		} else {
			atlas.mutex_failed = true;
		}
	}
	if (!atlas.mutex) {
		return NULL;
	}

	SDL_LockMutex(atlas.mutex);
	int x = 0, y = 0;
	int p = atlas_reserve(w, h, &x, &y);
	SDL_GPUTexture *tex = (p >= 0) ? atlas.pages[p].texture : NULL;
	if (p >= 0) {
		atlas.used_texels += (long long)w * h;
	}
	SDL_UnlockMutex(atlas.mutex);

	if (!tex) {
		return NULL;
	}

	if (!atlas_upload_region(tex, pixels, x, y, w, h)) {
		note("gpu_atlas: region upload failed: %s", SDL_GetError());
		/* the reserved region is wasted, but the caller can still fall
		 * back to a standalone texture */
		return NULL;
	}

	*out_x = x;
	*out_y = y;
	return tex;
}

void gpu_atlas_shutdown(void)
{
	if (sdlgpu) {
		for (int p = 0; p < atlas.num_pages; p++) {
			if (atlas.pages[p].texture) {
				SDL_ReleaseGPUTexture(sdlgpu, atlas.pages[p].texture);
			}
		}
	}
	if (atlas.mutex) {
		SDL_DestroyMutex(atlas.mutex);
	}
	memset(&atlas, 0, sizeof(atlas));
}

void gpu_atlas_get_stats(int *pages, long long *used_texels)
{
	if (pages) {
		*pages = atlas.num_pages;
	}
	if (used_texels) {
		*used_texels = atlas.used_texels;
	}
}
