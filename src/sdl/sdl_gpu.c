/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL GPU - Implementation
 *
 * SDL3 GPU API implementation providing hardware-accelerated rendering
 * with automatic fallback to SDL_Renderer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "sdl_gpu.h"
#include "sdl_gpu_post.h"
#include "sdl_gpu_shaderfx.h"
#include "sdl_gpu_glow.h"
#include "sdl_gpu_prim.h"
#include "sdl_gpu_atlas.h"
#include "sdl_gpu_text.h"
#include "astonia.h"

// ============================================================================
// Global State
// ============================================================================

// GPU rendering mode flag
bool use_gpu_rendering = false;

// Opt-in gate: the GPU path is EXPERIMENTAL and default-off. sdl_init only
// attempts gpu_init() when this was set (from the gpu_rendering extra option
// or the GO_GPU -o bit) before sdl_init runs. SDL_Renderer is the default.
bool gpu_rendering_requested = false;

// GPU device handle
SDL_GPUDevice *sdlgpu = NULL;

// Window reference (for swapchain operations)
static SDL_Window *gpu_window = NULL;

// Current frame state
static SDL_GPUCommandBuffer *current_cmd_buffer = NULL;
static SDL_GPUTexture *current_swapchain_texture = NULL;
static SDL_GPURenderPass *current_render_pass = NULL;
static bool using_postfx_this_frame = false;
static uint32_t current_swapchain_width = 0;
static uint32_t current_swapchain_height = 0;

// Offscreen render target currently bound as the pass target (NULL = screen).
// When set, gpu_get_swapchain_size() reports the target's size so the draw
// helpers map coordinates onto the offscreen texture instead of the screen.
static SDL_GPUTexture *current_offscreen_target = NULL;
static int current_offscreen_width = 0;
static int current_offscreen_height = 0;

// Debug counters
static int gpu_debug_frame_count = 0;
static int gpu_debug_draw_count = 0;
// Wall time from gpu_frame_begin to submit (render recording + submit
// only, excludes game logic and frame pacing) - for ASTONIA_GPU_STATS
static Uint64 gpu_frame_start_ns = 0;

// Graphics pipelines
static SDL_GPUGraphicsPipeline *pipelines[GPU_PIPELINE_COUNT] = {NULL};

// Default sampler
static SDL_GPUSampler *default_sampler = NULL;

// ============================================================================
// Texture upload staging ring
// ============================================================================
//
// Every sprite, atlas region and text string used to be uploaded through its
// own transfer buffer, copy pass and command-buffer submit. On Vulkan a
// transfer buffer is a sub-allocation, so that was merely wasteful; on D3D12
// every one is a committed resource (a kernel allocation, freed by another
// kernel call on release) and every submit an ExecuteCommandLists plus a
// fence signal - the Windows client fell apart whenever text or sprites
// churned. Uploads now stage into one persistent, cycled transfer buffer and
// go out in a single copy pass and submit: at frame begin, right before the
// frame's render command buffer is submitted, when the staging buffer fills,
// and before a texture with a pending upload is released. Rows are padded to
// a 256-byte pitch and every region starts at a 512-byte offset, which lets
// the D3D12 backend copy straight out of the staging buffer instead of
// re-packing each upload through yet another temporary resource.

#define UPLOAD_STAGING_BYTES (8u * 1024u * 1024u)
#define UPLOAD_MAX_PENDING   1024
#define UPLOAD_OFFSET_ALIGN  512u /* D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT */
#define UPLOAD_PITCH_ALIGN   256u /* D3D12_TEXTURE_DATA_PITCH_ALIGNMENT */

typedef struct upload_item {
	SDL_GPUTexture *texture;
	Uint32 offset; /* into the staging buffer */
	Uint32 pixels_per_row; /* padded row stride, in texels */
	int x, y, w, h;
} upload_item_t;

static struct {
	SDL_GPUTransferBuffer *staging;
	Uint32 capacity;
	uint8_t *mapped; /* non-NULL while a batch is open */
	Uint32 used;
	upload_item_t items[UPLOAD_MAX_PENDING];
	int count;
	SDL_Mutex *mutex;
	bool ready;
	bool map_failed_logged;
	int stat_uploads, stat_flushes, stat_fallbacks;
	long long stat_bytes;
} up = {0};

// Present mode requested through the vsync option. Swapchain parameters are
// only changed between frames (never while a swapchain texture is acquired),
// so a request made from the Options window is parked until the next
// gpu_frame_begin().
static SDL_GPUPresentMode wanted_present_mode = SDL_GPU_PRESENTMODE_VSYNC;
static SDL_GPUPresentMode active_present_mode = SDL_GPU_PRESENTMODE_VSYNC;
static bool present_mode_dirty = false;

static Uint32 upload_align(Uint32 v, Uint32 a)
{
	return (v + a - 1u) & ~(a - 1u);
}

static bool upload_init(void)
{
	SDL_GPUTransferBufferCreateInfo ti = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = UPLOAD_STAGING_BYTES};

	up.staging = SDL_CreateGPUTransferBuffer(sdlgpu, &ti);
	if (!up.staging) {
		note("gpu_init: upload staging buffer unavailable (%s) - uploads use per-texture transfer buffers",
		    SDL_GetError());
		return false;
	}
	up.mutex = SDL_CreateMutex();
	if (!up.mutex) {
		SDL_ReleaseGPUTransferBuffer(sdlgpu, up.staging);
		up.staging = NULL;
		return false;
	}
	up.capacity = UPLOAD_STAGING_BYTES;
	up.ready = true;
	return true;
}

/* Record and submit every staged upload. Caller holds up.mutex. */
static void upload_flush_locked(void)
{
	if (!up.mapped) {
		return;
	}
	SDL_UnmapGPUTransferBuffer(sdlgpu, up.staging);
	up.mapped = NULL;

	int n = up.count;
	up.count = 0;
	up.used = 0;
	if (n == 0) {
		return;
	}

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
	if (!cmd) {
		note("gpu_upload: command buffer acquire failed, %d uploads lost: %s", n, SDL_GetError());
		return;
	}
	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	if (!cp) {
		note("gpu_upload: copy pass failed, %d uploads lost: %s", n, SDL_GetError());
		SDL_CancelGPUCommandBuffer(cmd);
		return;
	}
	for (int i = 0; i < n; i++) {
		const upload_item_t *it = &up.items[i];
		SDL_GPUTextureTransferInfo src = {.transfer_buffer = up.staging,
		    .offset = it->offset,
		    .pixels_per_row = it->pixels_per_row,
		    .rows_per_layer = (Uint32)it->h};
		SDL_GPUTextureRegion dst = {.texture = it->texture,
		    .mip_level = 0,
		    .layer = 0,
		    .x = (Uint32)it->x,
		    .y = (Uint32)it->y,
		    .z = 0,
		    .w = (Uint32)it->w,
		    .h = (Uint32)it->h,
		    .d = 1};
		SDL_UploadToGPUTexture(cp, &src, &dst, false);
	}
	SDL_EndGPUCopyPass(cp);
	SDL_SubmitGPUCommandBuffer(cmd);
	up.stat_flushes++;
}

void gpu_upload_flush(void)
{
	if (!up.ready) {
		return;
	}
	SDL_LockMutex(up.mutex);
	upload_flush_locked();
	SDL_UnlockMutex(up.mutex);
}

bool gpu_upload_texture(SDL_GPUTexture *texture, const uint32_t *pixels, int x, int y, int w, int h)
{
	if (!up.ready || !sdlgpu || !texture || !pixels || x < 0 || y < 0 || w <= 0 || h <= 0) {
		return false;
	}

	Uint32 row_bytes = (Uint32)w * 4u;
	Uint32 pitch = upload_align(row_bytes, UPLOAD_PITCH_ALIGN);
	Uint32 bytes = pitch * (Uint32)h;
	if (bytes > up.capacity) {
		up.stat_fallbacks++; /* larger than the ring: the caller uploads it on its own */
		return false;
	}

	SDL_LockMutex(up.mutex);
	if (up.mapped &&
	    (upload_align(up.used, UPLOAD_OFFSET_ALIGN) + bytes > up.capacity || up.count >= UPLOAD_MAX_PENDING)) {
		upload_flush_locked();
	}
	if (!up.mapped) {
		/* cycle: a fresh backing buffer if the last batch is still in flight */
		up.mapped = SDL_MapGPUTransferBuffer(sdlgpu, up.staging, true);
		up.used = 0;
		up.count = 0;
		if (!up.mapped) {
			if (!up.map_failed_logged) {
				note("gpu_upload: staging map failed (%s) - falling back to per-texture uploads", SDL_GetError());
				up.map_failed_logged = true;
			}
			up.stat_fallbacks++;
			SDL_UnlockMutex(up.mutex);
			return false;
		}
	}

	Uint32 off = upload_align(up.used, UPLOAD_OFFSET_ALIGN);
	uint8_t *dst = up.mapped + off;
	if (pitch == row_bytes) {
		memcpy(dst, pixels, bytes);
	} else {
		for (int r = 0; r < h; r++) {
			memcpy(dst + (size_t)r * pitch, pixels + (size_t)r * (size_t)w, row_bytes);
		}
	}

	upload_item_t *it = &up.items[up.count++];
	it->texture = texture;
	it->offset = off;
	it->pixels_per_row = pitch / 4u;
	it->x = x;
	it->y = y;
	it->w = w;
	it->h = h;
	up.used = off + bytes;
	up.stat_uploads++;
	up.stat_bytes += bytes;
	SDL_UnlockMutex(up.mutex);
	return true;
}

/* A texture about to be released must not have an upload still waiting in
 * the ring: submit the batch first (SDL then defers the actual release until
 * that copy has executed). */
static void upload_forget(SDL_GPUTexture *texture)
{
	if (!up.ready || !texture) {
		return;
	}
	SDL_LockMutex(up.mutex);
	for (int i = 0; i < up.count; i++) {
		if (up.items[i].texture == texture) {
			upload_flush_locked();
			break;
		}
	}
	SDL_UnlockMutex(up.mutex);
}

static void upload_frame_reset_stats(void)
{
	up.stat_uploads = 0;
	up.stat_flushes = 0;
	up.stat_fallbacks = 0;
	up.stat_bytes = 0;
}

void gpu_upload_get_stats(int *uploads, int *flushes, int *fallbacks, long long *bytes)
{
	if (uploads) {
		*uploads = up.stat_uploads;
	}
	if (flushes) {
		*flushes = up.stat_flushes;
	}
	if (fallbacks) {
		*fallbacks = up.stat_fallbacks;
	}
	if (bytes) {
		*bytes = up.stat_bytes;
	}
}

static void upload_shutdown(void)
{
	if (!up.ready) {
		return;
	}
	gpu_upload_flush();
	SDL_WaitForGPUIdle(sdlgpu);
	SDL_ReleaseGPUTransferBuffer(sdlgpu, up.staging);
	SDL_DestroyMutex(up.mutex);
	memset(&up, 0, sizeof(up));
}

// ============================================================================
// Present mode (the vsync option)
// ============================================================================

static const char *present_mode_name(SDL_GPUPresentMode mode)
{
	switch (mode) {
	case SDL_GPU_PRESENTMODE_VSYNC:
		return "vsync";
	case SDL_GPU_PRESENTMODE_IMMEDIATE:
		return "immediate";
	case SDL_GPU_PRESENTMODE_MAILBOX:
		return "mailbox";
	default:
		return "unknown";
	}
}

static void apply_present_mode(void)
{
	if (!present_mode_dirty || !sdlgpu || !gpu_window) {
		return;
	}
	present_mode_dirty = false;
	if (wanted_present_mode == active_present_mode) {
		return;
	}
	if (!SDL_SetGPUSwapchainParameters(sdlgpu, gpu_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, wanted_present_mode)) {
		note("gpu: present mode %s refused: %s", present_mode_name(wanted_present_mode), SDL_GetError());
		wanted_present_mode = active_present_mode;
		return;
	}
	active_present_mode = wanted_present_mode;
	note("gpu: present mode %s", present_mode_name(active_present_mode));
}

bool gpu_set_vsync(bool on)
{
	if (!sdlgpu || !gpu_window) {
		return false;
	}
	SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
	if (!on) {
		/* mailbox: uncapped like immediate, but never tears */
		if (SDL_WindowSupportsGPUPresentMode(sdlgpu, gpu_window, SDL_GPU_PRESENTMODE_MAILBOX)) {
			mode = SDL_GPU_PRESENTMODE_MAILBOX;
		} else if (SDL_WindowSupportsGPUPresentMode(sdlgpu, gpu_window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
			mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
		} else {
			note("gpu: neither mailbox nor immediate presentation is supported here - vsync stays on");
			return false;
		}
	}
	wanted_present_mode = mode;
	present_mode_dirty = true;
	if (!current_cmd_buffer) {
		apply_present_mode(); /* between frames: switch right away */
	}
	return true;
}

// ============================================================================
// Device selection
// ============================================================================

// Vulkan first, on every platform that has it: its SDL backend sub-allocates
// buffers and textures from large memory blocks, whereas the D3D12 backend
// creates a committed resource - a kernel allocation - for each of them,
// so every sprite and text upload costs a kernel round trip there. Metal and
// D3D12 remain the fallbacks when no Vulkan driver is installed. A
// SDL_GPU_DRIVER hint (environment, or the gpu_driver extra option) is a
// deliberate choice and wins over the preference.
static SDL_GPUDevice *gpu_create_device(void)
{
	SDL_GPUDevice *dev;
	const char *hint = SDL_GetHint(SDL_HINT_GPU_DRIVER);

	if (hint && *hint) {
		dev = SDL_CreateGPUDevice(GPU_SHADER_FORMATS, false, NULL);
		if (dev) {
			return dev;
		}
		note("gpu_init: requested GPU driver \"%s\" unavailable (%s) - choosing automatically", hint, SDL_GetError());
		SDL_ResetHint(SDL_HINT_GPU_DRIVER);
	}
	dev = SDL_CreateGPUDevice(GPU_SHADER_FORMATS, false, "vulkan");
	if (dev) {
		return dev;
	}
	note("gpu_init: no Vulkan GPU device (%s) - trying the platform default backend", SDL_GetError());
	return SDL_CreateGPUDevice(GPU_SHADER_FORMATS, false, NULL);
}

// ============================================================================
// Initialization and Shutdown
// ============================================================================

bool gpu_init(SDL_Window *window)
{
	if (!window) {
		note("gpu_init: NULL window provided");
		return false;
	}

	gpu_window = window;

	// Try to create GPU device with all supported shader formats
	sdlgpu = gpu_create_device();

	if (!sdlgpu) {
		note("gpu_init: SDL_CreateGPUDevice failed: %s", SDL_GetError());
		note("gpu_init: Falling back to SDL_Renderer");
		use_gpu_rendering = false;
		return false;
	}

	// Claim window for GPU rendering
	if (!SDL_ClaimWindowForGPUDevice(sdlgpu, window)) {
		note("gpu_init: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
		SDL_DestroyGPUDevice(sdlgpu);
		sdlgpu = NULL;
		use_gpu_rendering = false;
		return false;
	}

	// Create default sampler
	default_sampler = gpu_sampler_create();
	if (!default_sampler) {
		note("gpu_init: Failed to create default sampler");
		SDL_ReleaseWindowFromGPUDevice(sdlgpu, window);
		SDL_DestroyGPUDevice(sdlgpu);
		sdlgpu = NULL;
		use_gpu_rendering = false;
		return false;
	}

	// Staged uploads (optional: without it every texture uploads on its own)
	upload_init();

	wanted_present_mode = SDL_GPU_PRESENTMODE_VSYNC;
	active_present_mode = SDL_GPU_PRESENTMODE_VSYNC;
	present_mode_dirty = false;

	use_gpu_rendering = true;
	note("gpu_init: GPU rendering enabled using %s", gpu_get_driver_name());

	return true;
}

void gpu_shutdown(void)
{
	if (!sdlgpu) {
		return;
	}

	// Submit whatever is still staged, then wait for it (and everything else)
	upload_shutdown();
	SDL_WaitForGPUIdle(sdlgpu);

	// Release pipelines
	gpu_pipelines_release();

	// Release default sampler
	if (default_sampler) {
		SDL_ReleaseGPUSampler(sdlgpu, default_sampler);
		default_sampler = NULL;
	}

	// Release window
	if (gpu_window) {
		SDL_ReleaseWindowFromGPUDevice(sdlgpu, gpu_window);
		gpu_window = NULL;
	}

	// Destroy device
	SDL_DestroyGPUDevice(sdlgpu);
	sdlgpu = NULL;
	use_gpu_rendering = false;

	note("gpu_shutdown: GPU rendering disabled");
}

bool gpu_is_active(void)
{
	return use_gpu_rendering && sdlgpu != NULL;
}

SDL_GPUShaderFormat gpu_preferred_shader_format(void)
{
	if (!sdlgpu) {
		return 0;
	}
	SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(sdlgpu);
	if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
		return SDL_GPU_SHADERFORMAT_SPIRV;
	}
	if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
		return SDL_GPU_SHADERFORMAT_DXIL;
	}
	if (formats & SDL_GPU_SHADERFORMAT_MSL) {
		return SDL_GPU_SHADERFORMAT_MSL;
	}
	return 0;
}

const char *gpu_shader_file_ext(SDL_GPUShaderFormat fmt)
{
	switch (fmt) {
	case SDL_GPU_SHADERFORMAT_DXIL:
		return "dxil";
	case SDL_GPU_SHADERFORMAT_MSL:
		return "msl";
	default:
		return "spv";
	}
}

const char *gpu_shader_entrypoint(SDL_GPUShaderFormat fmt)
{
	return (fmt == SDL_GPU_SHADERFORMAT_MSL) ? "main0" : "main";
}

// ============================================================================
// Frame Management
// ============================================================================

bool gpu_frame_begin(void)
{
	if (!gpu_is_active()) {
		return false;
	}

	// Reset frame state
	using_postfx_this_frame = false;
	current_offscreen_target = NULL;
	gpu_debug_draw_count = 0;
	upload_frame_reset_stats();

	// A vsync change from the Options window waits for this moment: the
	// swapchain is not touched while one of its textures is acquired
	apply_present_mode();

	// Uploads staged since the last frame ended (mod texture loads from event
	// handlers, preloads during a skipped frame) go out before this frame's
	// draws are recorded
	gpu_upload_flush();

	// Advance the atlas quarantine clock (region reclamation)
	gpu_atlas_frame_tick();

	// Reset per-frame batched-text stats
	gpu_text_frame_begin();

	// Acquire command buffer
	current_cmd_buffer = SDL_AcquireGPUCommandBuffer(sdlgpu);
	if (!current_cmd_buffer) {
		note("gpu_frame_begin: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
		return false;
	}

	// Wait for and acquire swapchain texture
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(current_cmd_buffer, gpu_window, &current_swapchain_texture,
	        &current_swapchain_width, &current_swapchain_height)) {
		note("gpu_frame_begin: SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
		SDL_CancelGPUCommandBuffer(current_cmd_buffer);
		current_cmd_buffer = NULL;
		return false;
	}

	if (!current_swapchain_texture) {
		// Window may be minimized, skip this frame
		SDL_CancelGPUCommandBuffer(current_cmd_buffer);
		current_cmd_buffer = NULL;
		return false;
	}

	// Start the render-span clock AFTER the swapchain wait so present
	// back-pressure does not pollute the CPU recording measurement
	gpu_frame_start_ns = SDL_GetTicksNS();

	// Try to use post-processing (renders to offscreen texture, then applies effects)
	if (gpu_postfx_is_enabled()) {
		current_render_pass = gpu_postfx_begin_scene(current_cmd_buffer);
		if (current_render_pass) {
			using_postfx_this_frame = true;
			// Set viewport for post-fx scene texture
			SDL_GPUViewport viewport = {.x = 0.0f,
			    .y = 0.0f,
			    .w = (float)current_swapchain_width,
			    .h = (float)current_swapchain_height,
			    .min_depth = 0.0f,
			    .max_depth = 1.0f};
			SDL_SetGPUViewport(current_render_pass, &viewport);

			gpu_shaderfx_frame_begin();
			gpu_glow_frame_begin();
			gpu_prim_batch_frame_begin();
			return true;
		}
		// Post-FX failed, fall through to direct swapchain rendering
	}

	// Direct swapchain rendering (fallback or post-fx not available)
	SDL_GPUColorTargetInfo color_target = {.texture = current_swapchain_texture,
	    .mip_level = 0,
	    .layer_or_depth_plane = 0,
	    .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
	    .load_op = SDL_GPU_LOADOP_CLEAR,
	    .store_op = SDL_GPU_STOREOP_STORE,
	    .resolve_texture = NULL,
	    .resolve_mip_level = 0,
	    .resolve_layer = 0,
	    .cycle = false,
	    .cycle_resolve_texture = false};

	current_render_pass = SDL_BeginGPURenderPass(current_cmd_buffer, &color_target, 1, NULL);
	if (!current_render_pass) {
		note("gpu_frame_begin: SDL_BeginGPURenderPass failed: %s", SDL_GetError());
		SDL_CancelGPUCommandBuffer(current_cmd_buffer);
		current_cmd_buffer = NULL;
		current_swapchain_texture = NULL;
		return false;
	}

	// Set viewport for swapchain
	SDL_GPUViewport viewport = {.x = 0.0f,
	    .y = 0.0f,
	    .w = (float)current_swapchain_width,
	    .h = (float)current_swapchain_height,
	    .min_depth = 0.0f,
	    .max_depth = 1.0f};
	SDL_SetGPUViewport(current_render_pass, &viewport);

	gpu_shaderfx_frame_begin();
	gpu_glow_frame_begin();
	gpu_prim_batch_frame_begin();

	return true;
}

void gpu_frame_end(void)
{
	if (!current_cmd_buffer) {
		return;
	}

	// Flush any pending batched glows and sprites before ending the render
	// pass. Glows first: they are additive and were recorded underneath
	// whatever sprite run is still pending.
	gpu_prim_batch_flush();
	gpu_glow_flush();
	gpu_shaderfx_flush();

	// End the main render pass
	if (current_render_pass) {
		if (using_postfx_this_frame) {
			// End scene render pass (renders to offscreen texture)
			gpu_postfx_end_scene(current_render_pass);
			current_render_pass = NULL;

			// Apply post-processing and render to swapchain
			gpu_postfx_present(current_cmd_buffer, current_swapchain_texture);
		} else {
			// Direct swapchain rendering - just end the pass
			SDL_EndGPURenderPass(current_render_pass);
			current_render_pass = NULL;
		}
	}

	// Upload this frame's batched instance data on its own command buffer,
	// submitted BEFORE the render command buffer: SDL_GPU executes command
	// buffers in submission order, so the copy lands before the draws
	// without any fence waits.
	gpu_shaderfx_submit_upload();
	gpu_glow_submit_upload();
	gpu_prim_batch_submit_upload();

	// Texture uploads staged during this frame (new sprites, atlas regions,
	// text strings) go out the same way: one copy pass, one submit, ahead
	// of the render command buffer that samples them
	gpu_upload_flush();

	// Optional A/B instrumentation: ASTONIA_GPU_STATS=1 logs draw-call and
	// batching counters once per second (~any fps) for perf comparisons.
	{
		static int stats_enabled = -1;
		if (stats_enabled < 0) {
			const char *env = SDL_getenv("ASTONIA_GPU_STATS");
			stats_enabled = (env && *env && *env != '0') ? 1 : 0;
		}
		if (stats_enabled) {
			static Uint64 last_report;
			static Uint64 render_ns_since;
			static int frames_since, draws_since, fx_draws_since, fx_sprites_since;
			static int text_runs_since, text_glyphs_since, text_fallbacks_since;
			static int glow_draws_since, glow_glows_since;
			static int prim_draws_since, prim_rects_since;
			static int uploads_since, upload_flushes_since, upload_fallbacks_since;
			static long long upload_bytes_since;
			int fxd, fxs, fxt, fxdirect;
			int truns, tglyphs, tfall;
			int gdraws, gglows;
			int pdraws, prects;
			int ups, upf, upfb;
			long long upb;
			gpu_shaderfx_get_stats(&fxd, &fxs, &fxt, &fxdirect);
			gpu_text_get_stats(&truns, &tglyphs, &tfall);
			gpu_glow_get_stats(&gdraws, &gglows);
			gpu_prim_batch_get_stats(&pdraws, &prects);
			gpu_upload_get_stats(&ups, &upf, &upfb, &upb);
			uploads_since += ups;
			upload_flushes_since += upf;
			upload_fallbacks_since += upfb;
			upload_bytes_since += upb;
			frames_since++;
			draws_since += gpu_debug_draw_count;
			fx_draws_since += fxd;
			fx_sprites_since += fxs;
			text_runs_since += truns;
			text_glyphs_since += tglyphs;
			text_fallbacks_since += tfall;
			glow_draws_since += gdraws;
			glow_glows_since += gglows;
			prim_draws_since += pdraws;
			prim_rects_since += prects;
			render_ns_since += SDL_GetTicksNS() - gpu_frame_start_ns;
			Uint64 now = SDL_GetTicks();
			if (last_report == 0) {
				last_report = now;
			}
			if (now - last_report >= 1000 && frames_since > 0) {
				int atlas_pages;
				long long atlas_texels;
				gpu_atlas_get_stats(&atlas_pages, &atlas_texels);
				note("GPU_STATS frames=%d avg_draws=%d avg_fx_draws=%d avg_fx_sprites=%d avg_text_runs=%d "
				     "avg_text_glyphs=%d avg_text_fallbacks=%d avg_glow_draws=%d avg_glows=%d "
				     "avg_prim_draws=%d avg_prim_rects=%d atlas_pages=%d "
				     "uploads=%d upload_flushes=%d upload_kb=%lld upload_fallbacks=%d present=%s "
				     "render_ms=%.2f ms/frame=%.2f",
				    frames_since, draws_since / frames_since, fx_draws_since / frames_since,
				    fx_sprites_since / frames_since, text_runs_since / frames_since, text_glyphs_since / frames_since,
				    text_fallbacks_since / frames_since, glow_draws_since / frames_since,
				    glow_glows_since / frames_since, prim_draws_since / frames_since, prim_rects_since / frames_since,
				    atlas_pages, uploads_since, upload_flushes_since, upload_bytes_since / 1024, upload_fallbacks_since,
				    present_mode_name(active_present_mode), (double)render_ns_since / 1e6 / (double)frames_since,
				    (double)(now - last_report) / (double)frames_since);
				last_report = now;
				frames_since = draws_since = fx_draws_since = fx_sprites_since = 0;
				text_runs_since = text_glyphs_since = text_fallbacks_since = 0;
				glow_draws_since = glow_glows_since = 0;
				prim_draws_since = prim_rects_since = 0;
				uploads_since = upload_flushes_since = upload_fallbacks_since = 0;
				upload_bytes_since = 0;
				render_ns_since = 0;
			}
		}
	}

	// Submit command buffer (this also presents the swapchain)
	if (!SDL_SubmitGPUCommandBuffer(current_cmd_buffer)) {
		note("gpu_frame_end: SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
	}

	gpu_debug_frame_count++;
	current_cmd_buffer = NULL;
	current_swapchain_texture = NULL;
	using_postfx_this_frame = false;
	current_offscreen_target = NULL;
}

SDL_GPUCommandBuffer *gpu_get_command_buffer(void)
{
	return current_cmd_buffer;
}

SDL_GPUTexture *gpu_get_swapchain_texture(void)
{
	return current_swapchain_texture;
}

SDL_GPURenderPass *gpu_get_render_pass(void)
{
	return current_render_pass;
}

void gpu_get_swapchain_size(int *width, int *height)
{
	if (current_offscreen_target) {
		if (width) {
			*width = current_offscreen_width;
		}
		if (height) {
			*height = current_offscreen_height;
		}
		return;
	}
	if (width) {
		*width = (int)current_swapchain_width;
	}
	if (height) {
		*height = (int)current_swapchain_height;
	}
}

bool gpu_set_render_target(SDL_GPUTexture *target, int width, int height, bool clear)
{
	if (!current_cmd_buffer) {
		return false; // only valid between gpu_frame_begin and gpu_frame_end
	}

	SDL_GPUTexture *tex;
	float vw, vh;

	if (target) {
		tex = target;
		vw = (float)width;
		vh = (float)height;
	} else {
		// Back to the screen: the post-fx scene texture when active,
		// otherwise the swapchain
		tex = using_postfx_this_frame ? gpu_postfx_get_scene_texture() : current_swapchain_texture;
		vw = (float)current_swapchain_width;
		vh = (float)current_swapchain_height;
	}
	if (!tex) {
		return false;
	}

	// End the current pass and open a new one aimed at the requested target.
	// The screen target resumes with LOADOP_LOAD so earlier drawing survives.
	if (current_render_pass) {
		gpu_prim_batch_flush();
		gpu_glow_flush();
		gpu_shaderfx_flush();
		SDL_EndGPURenderPass(current_render_pass);
		current_render_pass = NULL;
	}

	SDL_GPUColorTargetInfo color_target = {.texture = tex,
	    .mip_level = 0,
	    .layer_or_depth_plane = 0,
	    .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
	    .load_op = (target && clear) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
	    .store_op = SDL_GPU_STOREOP_STORE,
	    .resolve_texture = NULL,
	    .resolve_mip_level = 0,
	    .resolve_layer = 0,
	    .cycle = false,
	    .cycle_resolve_texture = false};

	current_render_pass = SDL_BeginGPURenderPass(current_cmd_buffer, &color_target, 1, NULL);
	if (!current_render_pass) {
		note("gpu_set_render_target: SDL_BeginGPURenderPass failed: %s", SDL_GetError());
		current_offscreen_target = NULL;
		return false;
	}

	SDL_GPUViewport viewport = {.x = 0.0f, .y = 0.0f, .w = vw, .h = vh, .min_depth = 0.0f, .max_depth = 1.0f};
	SDL_SetGPUViewport(current_render_pass, &viewport);


	current_offscreen_target = target;
	current_offscreen_width = width;
	current_offscreen_height = height;
	return true;
}

void gpu_debug_increment_draw_count(void)
{
	gpu_debug_draw_count++;
}

// ============================================================================
// Pipeline Management
// ============================================================================

bool gpu_pipelines_load(void)
{
	if (!gpu_is_active()) {
		return false;
	}

	// Pipelines will be created as shaders are loaded
	// For now, return success - actual loading happens in Phase 2
	note("gpu_pipelines_load: Pipeline loading deferred until shaders are available");
	return true;
}

void gpu_pipelines_release(void)
{
	if (!sdlgpu) {
		return;
	}

	for (int i = 0; i < GPU_PIPELINE_COUNT; i++) {
		if (pipelines[i]) {
			SDL_ReleaseGPUGraphicsPipeline(sdlgpu, pipelines[i]);
			pipelines[i] = NULL;
		}
	}
}

SDL_GPUGraphicsPipeline *gpu_get_pipeline(gpu_pipeline_id_t id)
{
	if (id < 0 || id >= GPU_PIPELINE_COUNT) {
		return NULL;
	}
	return pipelines[id];
}

// ============================================================================
// Texture Management
// ============================================================================

SDL_GPUTexture *gpu_texture_create(const uint32_t *pixels, int width, int height)
{
	if (!gpu_is_active() || !pixels || width <= 0 || height <= 0) {
		static int fail_log_count = 0;
		if (fail_log_count++ < 10) {
			note("gpu_texture_create: early fail - active=%d pixels=%d w=%d h=%d sdlgpu=%d use_gpu=%d", gpu_is_active(),
			    pixels != NULL, width, height, sdlgpu != NULL, use_gpu_rendering);
		}
		return NULL;
	}

	// Create texture
	SDL_GPUTextureCreateInfo tex_info = {.type = SDL_GPU_TEXTURETYPE_2D,
	    .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
	    .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
	    .width = (Uint32)width,
	    .height = (Uint32)height,
	    .layer_count_or_depth = 1,
	    .num_levels = 1,
	    .sample_count = SDL_GPU_SAMPLECOUNT_1};

	SDL_GPUTexture *texture = SDL_CreateGPUTexture(sdlgpu, &tex_info);
	if (!texture) {
		note("gpu_texture_create: SDL_CreateGPUTexture failed: %s", SDL_GetError());
		return NULL;
	}

	// The staging ring batches this with every other upload of the frame
	if (gpu_upload_texture(texture, pixels, 0, 0, width, height)) {
		return texture;
	}

	// No ring (init failed, map failed) or the image is larger than the ring:
	// a dedicated transfer buffer and submit, as before
	SDL_GPUTransferBufferCreateInfo transfer_info = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
	    .size = (Uint32)((size_t)width * (size_t)height * sizeof(uint32_t))};

	SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(sdlgpu, &transfer_info);
	if (!transfer) {
		note("gpu_texture_create: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
		SDL_ReleaseGPUTexture(sdlgpu, texture);
		return NULL;
	}

	// Map and copy pixel data
	void *mapped = SDL_MapGPUTransferBuffer(sdlgpu, transfer, false);
	if (!mapped) {
		note("gpu_texture_create: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
		SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
		SDL_ReleaseGPUTexture(sdlgpu, texture);
		return NULL;
	}

	memcpy(mapped, pixels, (size_t)width * (size_t)height * sizeof(uint32_t));
	SDL_UnmapGPUTransferBuffer(sdlgpu, transfer);

	// Upload to GPU
	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sdlgpu);
	if (!cmd) {
		note("gpu_texture_create: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
		SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
		SDL_ReleaseGPUTexture(sdlgpu, texture);
		return NULL;
	}

	SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
	if (!copy_pass) {
		note("gpu_texture_create: SDL_BeginGPUCopyPass failed: %s", SDL_GetError());
		SDL_CancelGPUCommandBuffer(cmd);
		SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);
		SDL_ReleaseGPUTexture(sdlgpu, texture);
		return NULL;
	}

	SDL_GPUTextureTransferInfo src = {
	    .transfer_buffer = transfer, .offset = 0, .pixels_per_row = (Uint32)width, .rows_per_layer = (Uint32)height};

	SDL_GPUTextureRegion dst = {.texture = texture,
	    .mip_level = 0,
	    .layer = 0,
	    .x = 0,
	    .y = 0,
	    .z = 0,
	    .w = (Uint32)width,
	    .h = (Uint32)height,
	    .d = 1};

	SDL_UploadToGPUTexture(copy_pass, &src, &dst, false);
	SDL_EndGPUCopyPass(copy_pass);

	SDL_SubmitGPUCommandBuffer(cmd);

	// Release transfer buffer (texture data is now on GPU)
	SDL_ReleaseGPUTransferBuffer(sdlgpu, transfer);

	return texture;
}

void gpu_texture_destroy(SDL_GPUTexture *texture)
{
	if (texture && sdlgpu) {
		upload_forget(texture);
		SDL_ReleaseGPUTexture(sdlgpu, texture);
	}
}

SDL_GPUSampler *gpu_sampler_create(void)
{
	// Note: Don't use gpu_is_active() here - this is called during init
	// before use_gpu_rendering is set to true
	if (!sdlgpu) {
		return NULL;
	}

	SDL_GPUSamplerCreateInfo sampler_info = {.min_filter = SDL_GPU_FILTER_LINEAR,
	    .mag_filter = SDL_GPU_FILTER_LINEAR,
	    .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
	    .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	    .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	    .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	    .mip_lod_bias = 0.0f,
	    .max_anisotropy = 1.0f,
	    .compare_op = SDL_GPU_COMPAREOP_NEVER,
	    .min_lod = 0.0f,
	    .max_lod = 1.0f,
	    .enable_anisotropy = false,
	    .enable_compare = false};

	return SDL_CreateGPUSampler(sdlgpu, &sampler_info);
}

// ============================================================================
// Render Targets
// ============================================================================

SDL_GPUTexture *gpu_render_target_create(int width, int height)
{
	if (!gpu_is_active() || width <= 0 || height <= 0) {
		return NULL;
	}

	SDL_GPUTextureCreateInfo tex_info = {.type = SDL_GPU_TEXTURETYPE_2D,
	    .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
	    .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
	    .width = (Uint32)width,
	    .height = (Uint32)height,
	    .layer_count_or_depth = 1,
	    .num_levels = 1,
	    .sample_count = SDL_GPU_SAMPLECOUNT_1};

	SDL_GPUTexture *texture = SDL_CreateGPUTexture(sdlgpu, &tex_info);
	if (!texture) {
		note("gpu_render_target_create: SDL_CreateGPUTexture failed: %s", SDL_GetError());
	}

	return texture;
}

SDL_GPURenderPass *gpu_render_target_begin(SDL_GPUTexture *target, const SDL_FColor *clear_color)
{
	if (!current_cmd_buffer) {
		note("gpu_render_target_begin: No command buffer active");
		return NULL;
	}

	// Use swapchain if target is NULL
	SDL_GPUTexture *render_target = target ? target : current_swapchain_texture;
	if (!render_target) {
		note("gpu_render_target_begin: No render target available");
		return NULL;
	}

	SDL_GPUColorTargetInfo color_target = {.texture = render_target,
	    .mip_level = 0,
	    .layer_or_depth_plane = 0,
	    .clear_color = clear_color ? *clear_color : (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f},
	    .load_op = clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
	    .store_op = SDL_GPU_STOREOP_STORE,
	    .resolve_texture = NULL,
	    .resolve_mip_level = 0,
	    .resolve_layer = 0,
	    .cycle = false,
	    .cycle_resolve_texture = false};

	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(current_cmd_buffer, &color_target, 1, NULL);
	if (!pass) {
		note("gpu_render_target_begin: SDL_BeginGPURenderPass failed: %s", SDL_GetError());
	}

	return pass;
}

void gpu_render_target_end(SDL_GPURenderPass *pass)
{
	if (pass) {
		SDL_EndGPURenderPass(pass);
	}
}

// ============================================================================
// Debug and Diagnostics
// ============================================================================

const char *gpu_get_driver_name(void)
{
	if (!sdlgpu) {
		return "none";
	}
	return SDL_GetGPUDeviceDriver(sdlgpu);
}

void gpu_dump(FILE *fp)
{
	fprintf(fp, "GPU State:\n");
	fprintf(fp, "  use_gpu_rendering: %s\n", use_gpu_rendering ? "true" : "false");
	fprintf(fp, "  sdlgpu: %p\n", (void *)sdlgpu);
	fprintf(fp, "  driver: %s\n", gpu_get_driver_name());
	fprintf(fp, "  present_mode: %s\n", present_mode_name(active_present_mode));
	fprintf(fp, "  upload_ring: %s (%u KB staging)\n", up.ready ? "active" : "off", up.capacity / 1024u);

	if (sdlgpu) {
		// Report supported shader formats
		fprintf(fp, "  shader_formats:");
		SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(sdlgpu);
		if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
			fprintf(fp, " SPIRV");
		}
		if (formats & SDL_GPU_SHADERFORMAT_DXBC) {
			fprintf(fp, " DXBC");
		}
		if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
			fprintf(fp, " DXIL");
		}
		if (formats & SDL_GPU_SHADERFORMAT_MSL) {
			fprintf(fp, " MSL");
		}
		if (formats & SDL_GPU_SHADERFORMAT_METALLIB) {
			fprintf(fp, " METALLIB");
		}
		fprintf(fp, "\n");

		// Report pipeline status
		fprintf(fp, "  pipelines:\n");
		const char *pipeline_names[] = {"sprite", "primitive", "postfx"};
		for (int i = 0; i < GPU_PIPELINE_COUNT; i++) {
			fprintf(fp, "    %s: %s\n", pipeline_names[i], pipelines[i] ? "loaded" : "not loaded");
		}
	}

	fprintf(fp, "\n");
}
