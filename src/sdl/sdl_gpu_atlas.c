/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL3 GPU Base-Texture Atlas - see sdl_gpu_atlas.h.
 *
 * Phase 2 adds region reclamation: gpu_atlas_release() returns a
 * region to its shelf's free-span list so churning entries (evicted
 * sprites, cached text strings) no longer leak page space. Freed spans
 * pass through a frame-stamped quarantine first: an upload into a
 * reused region is submitted immediately, but the draws that reference
 * the OLD content are only submitted at frame end - reusing a span in
 * the same frame it was freed would corrupt those pending draws.
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

/* free spans per page (shared pool for all its shelves) */
#define MAX_SPANS_PER_PAGE 1024
/* released regions parked until their eviction frame is safely retired */
#define MAX_QUARANTINE 4096
/* frames a released span sits in quarantine before reuse (covers the
 * instance-ring depth of the batcher: draws recorded this frame may
 * still reference the old texels) */
#define QUARANTINE_FRAMES 3

/* region registry so release only needs (texture, x, y) */
#define REGION_TABLE_SIZE    32768 /* power of two */
#define REGION_KEY_EMPTY     0u
#define REGION_KEY_TOMBSTONE 0xFFFFFFFFu

typedef struct atlas_span {
	uint16_t x, w;
	int16_t next; /* index into the page's span pool, -1 = end */
} atlas_span_t;

typedef struct atlas_shelf {
	int y; /* top of the shelf in the page */
	int h; /* quantized shelf height */
	int next_x; /* next free x position (bump allocator) */
	int16_t free_head; /* sorted free-span list, -1 = empty */
} atlas_shelf_t;

typedef struct atlas_page {
	SDL_GPUTexture *texture;
	atlas_shelf_t shelves[MAX_SHELVES_PER_PAGE];
	int num_shelves;
	int next_shelf_y; /* top of the unused area below the shelves */

	atlas_span_t spans[MAX_SPANS_PER_PAGE];
	int16_t span_free_head; /* pool freelist */
	bool span_pool_init;
} atlas_page_t;

typedef struct atlas_region {
	uint32_t key; /* (page+1)<<22 | y<<11 | x; 0 = empty, ~0 = tombstone */
	uint16_t w, h;
} atlas_region_t;

typedef struct atlas_quarantined {
	uint8_t page;
	uint16_t x, y, w;
	uint32_t frame;
	bool used;
} atlas_quarantined_t;

static struct {
	atlas_page_t pages[GPU_ATLAS_MAX_PAGES];
	int num_pages;
	long long used_texels;
	SDL_Mutex *mutex;
	bool mutex_failed;

	atlas_region_t regions[REGION_TABLE_SIZE];
	atlas_quarantined_t quarantine[MAX_QUARANTINE];
	int quarantine_count; /* occupied entries (sparse) */
	uint32_t frame; /* gpu_atlas_frame_tick counter */
	bool span_overflow_warned;
} atlas = {0};

/* ==================================================================== */
/* region registry                                                      */
/* ==================================================================== */

static uint32_t region_key(int page, int x, int y)
{
	return (uint32_t)(((page + 1) << 22) | (y << 11) | x);
}

static uint32_t region_hash(uint32_t key)
{
	key *= 2654435761u;
	return key & (REGION_TABLE_SIZE - 1);
}

static void region_insert(int page, int x, int y, int w, int h)
{
	uint32_t key = region_key(page, x, y);
	uint32_t i = region_hash(key);

	for (int probe = 0; probe < REGION_TABLE_SIZE; probe++, i = (i + 1) & (REGION_TABLE_SIZE - 1)) {
		if (atlas.regions[i].key == REGION_KEY_EMPTY || atlas.regions[i].key == REGION_KEY_TOMBSTONE) {
			atlas.regions[i].key = key;
			atlas.regions[i].w = (uint16_t)w;
			atlas.regions[i].h = (uint16_t)h;
			return;
		}
	}
	/* table full: the region simply becomes non-reclaimable */
}

/* Find and remove; returns 0 when unknown. */
static int region_remove(int page, int x, int y, int *out_w, int *out_h)
{
	uint32_t key = region_key(page, x, y);
	uint32_t i = region_hash(key);

	for (int probe = 0; probe < REGION_TABLE_SIZE; probe++, i = (i + 1) & (REGION_TABLE_SIZE - 1)) {
		if (atlas.regions[i].key == REGION_KEY_EMPTY) {
			return 0;
		}
		if (atlas.regions[i].key == key) {
			*out_w = atlas.regions[i].w;
			*out_h = atlas.regions[i].h;
			atlas.regions[i].key = REGION_KEY_TOMBSTONE;
			return 1;
		}
	}
	return 0;
}

/* ==================================================================== */
/* span pool / free lists                                               */
/* ==================================================================== */

static void span_pool_init(atlas_page_t *page)
{
	if (page->span_pool_init) {
		return;
	}
	for (int i = 0; i < MAX_SPANS_PER_PAGE; i++) {
		page->spans[i].next = (int16_t)(i + 1);
	}
	page->spans[MAX_SPANS_PER_PAGE - 1].next = -1;
	page->span_free_head = 0;
	page->span_pool_init = true;
}

static int16_t span_alloc(atlas_page_t *page)
{
	span_pool_init(page);
	int16_t s = page->span_free_head;
	if (s >= 0) {
		page->span_free_head = page->spans[s].next;
	}
	return s;
}

static void span_free(atlas_page_t *page, int16_t s)
{
	page->spans[s].next = page->span_free_head;
	page->span_free_head = s;
}

/* Return span (x, w) to the shelf's sorted free list, coalescing with
 * neighbours. */
static void shelf_add_free_span(atlas_page_t *page, atlas_shelf_t *shelf, int x, int w)
{
	int16_t *link = &shelf->free_head;

	span_pool_init(page);

	while (*link >= 0 && page->spans[*link].x < x) {
		link = &page->spans[*link].next;
	}

	int16_t succ = *link;
	if (succ >= 0 && x + w == page->spans[succ].x) {
		/* grow the successor leftwards instead of allocating a span */
		page->spans[succ].x = (uint16_t)x;
		page->spans[succ].w = (uint16_t)(page->spans[succ].w + w);
	} else {
		int16_t s = span_alloc(page);
		if (s < 0) {
			if (!atlas.span_overflow_warned) {
				note("gpu_atlas: span pool exhausted, leaking a region (page full of holes?)");
				atlas.span_overflow_warned = true;
			}
			return;
		}
		page->spans[s].x = (uint16_t)x;
		page->spans[s].w = (uint16_t)w;
		page->spans[s].next = succ;
		*link = s;
	}

	/* single coalescing pass over the (short) list merges any remaining
	 * adjacency (the predecessor case) without needing back-links */
	int16_t cur = shelf->free_head;
	while (cur >= 0 && page->spans[cur].next >= 0) {
		int16_t nxt = page->spans[cur].next;
		if (page->spans[cur].x + page->spans[cur].w == page->spans[nxt].x) {
			page->spans[cur].w = (uint16_t)(page->spans[cur].w + page->spans[nxt].w);
			page->spans[cur].next = page->spans[nxt].next;
			span_free(page, nxt);
		} else {
			cur = nxt;
		}
	}
}

/* First-fit allocation from a shelf's free spans. Returns x or -1. */
static int shelf_alloc_from_spans(atlas_page_t *page, atlas_shelf_t *shelf, int w)
{
	int16_t *link = &shelf->free_head;

	while (*link >= 0) {
		atlas_span_t *sp = &page->spans[*link];
		if (sp->w >= w) {
			int x = sp->x;
			sp->x = (uint16_t)(sp->x + w);
			sp->w = (uint16_t)(sp->w - w);
			if (sp->w == 0) {
				int16_t dead = *link;
				*link = sp->next;
				span_free(page, dead);
			}
			return x;
		}
		link = &page->spans[*link].next;
	}
	return -1;
}

/* ==================================================================== */
/* page / shelf allocation                                              */
/* ==================================================================== */

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

		for (int s = 0; s < page->num_shelves; s++) {
			atlas_shelf_t *shelf = &page->shelves[s];
			if (shelf->h != hq) {
				continue;
			}
			/* reclaimed spans first, then the bump allocator */
			int x = shelf_alloc_from_spans(page, shelf, w);
			if (x >= 0) {
				*out_x = x;
				*out_y = shelf->y;
				return p;
			}
			if (shelf->next_x + w <= GPU_ATLAS_PAGE_SIZE) {
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
			shelf->free_head = -1;
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
		page->shelves[0].free_head = -1;
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

/* ==================================================================== */
/* public API                                                           */
/* ==================================================================== */

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
		region_insert(p, x, y, w, h);
	}
	SDL_UnlockMutex(atlas.mutex);

	if (!tex) {
		return NULL;
	}

	if (!atlas_upload_region(tex, pixels, x, y, w, h)) {
		note("gpu_atlas: region upload failed: %s", SDL_GetError());
		/* return the reservation so it is not leaked */
		gpu_atlas_release(tex, x, y);
		return NULL;
	}

	*out_x = x;
	*out_y = y;
	return tex;
}

void gpu_atlas_release(SDL_GPUTexture *page_texture, int x, int y)
{
	if (!atlas.mutex || !page_texture) {
		return;
	}

	SDL_LockMutex(atlas.mutex);

	int p;
	for (p = 0; p < atlas.num_pages; p++) {
		if (atlas.pages[p].texture == page_texture) {
			break;
		}
	}
	if (p == atlas.num_pages) {
		SDL_UnlockMutex(atlas.mutex);
		return; /* not an atlas page */
	}

	int w = 0, h = 0;
	if (!region_remove(p, x, y, &w, &h)) {
		note("gpu_atlas: release of unknown region page %d (%d,%d)", p, x, y);
		SDL_UnlockMutex(atlas.mutex);
		return;
	}

	atlas.used_texels -= (long long)w * h;

	/* park the span; gpu_atlas_frame_tick promotes it to the shelf free
	 * list once no in-flight frame can reference the old texels */
	int q;
	for (q = 0; q < MAX_QUARANTINE; q++) {
		if (!atlas.quarantine[q].used) {
			atlas.quarantine[q].used = true;
			atlas.quarantine[q].page = (uint8_t)p;
			atlas.quarantine[q].x = (uint16_t)x;
			atlas.quarantine[q].y = (uint16_t)y;
			atlas.quarantine[q].w = (uint16_t)w;
			atlas.quarantine[q].frame = atlas.frame;
			atlas.quarantine_count++;
			break;
		}
	}
	if (q == MAX_QUARANTINE && !atlas.span_overflow_warned) {
		note("gpu_atlas: quarantine full, leaking a region");
		atlas.span_overflow_warned = true;
	}

	SDL_UnlockMutex(atlas.mutex);
}

void gpu_atlas_frame_tick(void)
{
	if (!atlas.mutex) {
		return;
	}

	SDL_LockMutex(atlas.mutex);
	atlas.frame++;

	if (atlas.quarantine_count > 0) {
		for (int q = 0; q < MAX_QUARANTINE; q++) {
			atlas_quarantined_t *e = &atlas.quarantine[q];
			if (!e->used || atlas.frame - e->frame < QUARANTINE_FRAMES) {
				continue;
			}
			atlas_page_t *page = &atlas.pages[e->page];
			for (int s = 0; s < page->num_shelves; s++) {
				if (page->shelves[s].y == e->y) {
					shelf_add_free_span(page, &page->shelves[s], e->x, e->w);
					break;
				}
			}
			e->used = false;
			atlas.quarantine_count--;
		}
	}
	SDL_UnlockMutex(atlas.mutex);
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
