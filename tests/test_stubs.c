/*
 * Test Stubs - Minimal implementations of game functions for unit testing
 *
 * These stubs allow SDL code to link without the full game engine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_keycode.h>

// ============================================================================
// Logging stubs
// ============================================================================

void note(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
}

char *fail(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fprintf(stderr, "FAIL: ");
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
	return "test failure";
}

void paranoia(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fprintf(stderr, "PARANOIA: ");
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
	abort(); // Fail fast on paranoia checks
}

void warn(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fprintf(stderr, "WARN: ");
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
}

// ============================================================================
// Game state stubs
// ============================================================================

int quit = 0;
uint64_t game_options = 0;
char *localdata = NULL;
int xmemcheck_failed = 0;

// SDL worker thread globals (defined in sdl_core.c, not here)
// extern SDL_AtomicInt worker_quit;
// extern SDL_Thread **worker_threads;
// extern struct zip_handles *worker_zips;

// ============================================================================
// Render stubs
// ============================================================================

void render_set_offset(int x __attribute__((unused)), int y __attribute__((unused)))
{
	// No-op in tests
}

// ============================================================================
// GUI stubs
// ============================================================================

void gui_sdl_mouseproc(float x __attribute__((unused)), float y __attribute__((unused)), int b __attribute__((unused)))
{
	// No-op in tests
}

void gui_sdl_keyproc(SDL_Keycode key __attribute__((unused)), SDL_Keymod mod __attribute__((unused)))
{
	// No-op in tests
}

void context_keyup(SDL_Keycode key __attribute__((unused))) {}

void input_keyup(SDL_Keycode key __attribute__((unused))) {}

int cmd_is_active(void)
{
	return 0;
}

void gamepad_init(void) {}

void gamepad_shutdown(void) {}

void gamepad_tick(void) {}

void gamepad_on_added(uint32_t id __attribute__((unused))) {}

void gamepad_on_removed(uint32_t id __attribute__((unused))) {}

void gamepad_button_down(int button __attribute__((unused))) {}

void gamepad_button_up(int button __attribute__((unused))) {}

void gamepad_axis_motion(int axis __attribute__((unused)), int16_t value __attribute__((unused))) {}

void cmd_proc(int key __attribute__((unused)))
{
	// No-op in tests
}

int amod_textinput(int key __attribute__((unused)))
{
	// No mods in tests - never consume
	return 0;
}

void display_messagebox(const char *title __attribute__((unused)), const char *msg __attribute__((unused)))
{
	fprintf(stderr, "MessageBox: %s - %s\n", title ? title : "(no title)", msg ? msg : "(no message)");
}

// ============================================================================
// Audio stubs (SDL3_mixer)
// ============================================================================

// Prevent audio initialization in tests (game_options has GO_SOUND disabled)
// These stubs are here in case the linker needs them

// ============================================================================
// Random number stub
// ============================================================================

int rrand(int min, int max)
{
	if (max <= min) {
		return min;
	}
	return min + (rand() % (max - min + 1));
}

// ============================================================================
// Additional SDL stubs for render operations
// ============================================================================

// Note: SDL_SetRenderDrawBlendMode and other render stubs are now in sdl_test.c
// with render call counters for test verification.

bool SDL_SetTextureBlendMode(
    SDL_Texture *texture __attribute__((unused)), SDL_BlendMode blendMode __attribute__((unused)))
{
	return true;
}

bool SDL_SetTextureAlphaMod(SDL_Texture *texture __attribute__((unused)), uint8_t alpha __attribute__((unused)))
{
	return true;
}

// ============================================================================
// Sprite config stubs (sdl_image.c now depends on these)
// ============================================================================

int sprite_config_do_smoothify(unsigned int sprite __attribute__((unused)))
{
	return -1; /* No config: let caller use default */
}

int sprite_config_drop_alpha(unsigned int sprite __attribute__((unused)))
{
	return 0; /* No drop_alpha */
}

// ============================================================================
// SDL_GPU stubs - the tests exercise the SDL_Renderer path only; the GPU
// path is opt-in (gpu_rendering_requested, default off) and never entered
// here, so all gpu_* entry points are inert.
// ============================================================================

#include "sdl/sdl_gpu.h"
#include "sdl/sdl_gpu_draw.h"
#include "sdl/sdl_gpu_post.h"
#include "sdl/sdl_gpu_shaderfx.h"

bool use_gpu_rendering = false;
bool gpu_rendering_requested = false;
SDL_GPUDevice *sdlgpu = NULL;

// Shader-effects path (sdl_gpu_shaderfx.c / sdl_gpu_atlas.c are not
// linked into the tests)
#include "sdl/sdl_gpu_atlas.h"

bool gpu_shaderfx_requested = false;

SDL_GPUTexture *gpu_atlas_insert(const uint32_t *pixels __attribute__((unused)), int w __attribute__((unused)),
    int h __attribute__((unused)), int *out_x __attribute__((unused)), int *out_y __attribute__((unused)))
{
	return NULL;
}

void gpu_atlas_shutdown(void) {}

void gpu_atlas_release(SDL_GPUTexture *page_texture __attribute__((unused)), int x __attribute__((unused)),
    int y __attribute__((unused)))
{
}

void gpu_atlas_frame_tick(void) {}

void gpu_atlas_get_stats(int *pages, long long *used_texels)
{
	if (pages) {
		*pages = 0;
	}
	if (used_texels) {
		*used_texels = 0;
	}
}

bool gpu_shaderfx_init(void)
{
	return false;
}

void gpu_shaderfx_shutdown(void) {}

bool gpu_shaderfx_ready(void)
{
	return false;
}

void gpu_shaderfx_frame_begin(void) {}
void gpu_shaderfx_flush(void) {}
void gpu_shaderfx_submit_upload(void) {}
void gpu_shaderfx_direct_draw_barrier(void) {}

int gpu_shaderfx_plain_quad(SDL_GPUTexture *tex __attribute__((unused)), float dest_x __attribute__((unused)),
    float dest_y __attribute__((unused)), float dest_w __attribute__((unused)), float dest_h __attribute__((unused)),
    int src_x __attribute__((unused)), int src_y __attribute__((unused)), int src_w __attribute__((unused)),
    int src_h __attribute__((unused)), int r __attribute__((unused)), int g __attribute__((unused)),
    int b __attribute__((unused)), int alpha __attribute__((unused)))
{
	return 0;
}

int gpu_shaderfx_capacity(void)
{
	return 0;
}

int sdl_blit_fx(int cache_index __attribute__((unused)), const gpu_fx_draw_t *d __attribute__((unused)),
    int sx __attribute__((unused)), int sy __attribute__((unused)), int clipsx __attribute__((unused)),
    int clipsy __attribute__((unused)), int clipex __attribute__((unused)), int clipey __attribute__((unused)),
    int x_offset __attribute__((unused)), int y_offset __attribute__((unused)))
{
	return 0;
}

void gpu_shaderfx_get_stats(int *draws, int *sprites, int *tex_flushes, int *direct_flushes)
{
	if (draws) {
		*draws = 0;
	}
	if (sprites) {
		*sprites = 0;
	}
	if (tex_flushes) {
		*tex_flushes = 0;
	}
	if (direct_flushes) {
		*direct_flushes = 0;
	}
}

bool gpu_init(SDL_Window *window __attribute__((unused)))
{
	return false;
}

void gpu_shutdown(void) {}

bool gpu_is_active(void)
{
	return false;
}

bool gpu_frame_begin(void)
{
	return false;
}

void gpu_frame_end(void) {}

void gpu_dump(FILE *fp __attribute__((unused))) {}

SDL_GPUTexture *gpu_texture_create(
    const uint32_t *pixels __attribute__((unused)), int width __attribute__((unused)),
    int height __attribute__((unused)))
{
	return NULL;
}

void gpu_texture_destroy(SDL_GPUTexture *texture __attribute__((unused))) {}

bool gpu_draw_init(int screen_width __attribute__((unused)), int screen_height __attribute__((unused)))
{
	return false;
}

void gpu_draw_shutdown(void) {}

bool gpu_draw_is_available(void)
{
	return false;
}

#include "sdl/sdl_gpu_glow.h"

/* Glow batch (sdl_gpu_glow.c is not linked into the tests). Same idea as
 * the prim switches below: test_glow_available gates gpu_glow_ready() and
 * the counters record what the glow path would have emitted, so the
 * effect call sites can be driven without a GPU device. */
bool test_glow_available = false;
int test_glow_count = 0;
gpu_glow_instance_t test_glow_last = {{0}, {0}, {0}};

void test_glow_reset_counters(void)
{
	test_glow_count = 0;
	memset(&test_glow_last, 0, sizeof(test_glow_last));
}

bool gpu_glow_init(void)
{
	return test_glow_available;
}

void gpu_glow_shutdown(void) {}

bool gpu_glow_ready(void)
{
	return test_glow_available;
}

void gpu_glow_frame_begin(void) {}
void gpu_glow_flush(void) {}
void gpu_glow_submit_upload(void) {}
void gpu_glow_direct_draw_barrier(void) {}

void gpu_glow_get_stats(int *draws, int *glows)
{
	if (draws) {
		*draws = 0;
	}
	if (glows) {
		*glows = test_glow_count;
	}
}

bool gpu_glow_add(float x0, float y0, float x1, float y1, float radius, float core, float r, float g, float b,
    float intensity)
{
	if (!test_glow_available) {
		return false;
	}
	test_glow_last.seg[0] = x0;
	test_glow_last.seg[1] = y0;
	test_glow_last.seg[2] = x1;
	test_glow_last.seg[3] = y1;
	test_glow_last.shape[0] = radius;
	test_glow_last.shape[1] = core;
	test_glow_last.color[0] = r;
	test_glow_last.color[1] = g;
	test_glow_last.color[2] = b;
	test_glow_last.color[3] = intensity;
	test_glow_count++;
	return true;
}

/* Tests flip these to exercise the GPU branches of sdl_draw.c without a real
 * GPU device: test_gpu_prim_available gates the *_is_available() checks and
 * test_gpu_rect_count records what the GPU path would have drawn. */
bool test_gpu_prim_available = false;
int test_gpu_rect_count = 0;

void test_gpu_reset_counters(void)
{
	test_gpu_rect_count = 0;
}

bool gpu_draw_prim_is_available(void)
{
	return test_gpu_prim_available;
}

bool gpu_draw_line_is_available(void)
{
	return false;
}

void gpu_draw_texture(SDL_GPUTexture *texture __attribute__((unused)),
    const SDL_FRect *dest __attribute__((unused)), const SDL_FRect *src __attribute__((unused)),
    int tex_width __attribute__((unused)), int tex_height __attribute__((unused)),
    const float *color_mod __attribute__((unused)), int alpha __attribute__((unused)))
{
}

void gpu_draw_texture_rot45(SDL_GPUTexture *texture __attribute__((unused)),
    const SDL_FRect *dest __attribute__((unused)), const SDL_FRect *src __attribute__((unused)),
    int tex_width __attribute__((unused)), int tex_height __attribute__((unused)),
    const float *color_mod __attribute__((unused)), int alpha __attribute__((unused)))
{
}

void gpu_draw_rect(float x __attribute__((unused)), float y __attribute__((unused)),
    float w __attribute__((unused)), float h __attribute__((unused)), float r __attribute__((unused)),
    float g __attribute__((unused)), float b __attribute__((unused)), float a __attribute__((unused)))
{
	test_gpu_rect_count++;
}

void gpu_draw_line(float x1 __attribute__((unused)), float y1 __attribute__((unused)),
    float x2 __attribute__((unused)), float y2 __attribute__((unused)), float r __attribute__((unused)),
    float g __attribute__((unused)), float b __attribute__((unused)), float a __attribute__((unused)))
{
}

bool gpu_draw_tri_is_available(void)
{
	return false;
}

void gpu_draw_triangle(float x0 __attribute__((unused)), float y0 __attribute__((unused)),
    float x1 __attribute__((unused)), float y1 __attribute__((unused)), float x2 __attribute__((unused)),
    float y2 __attribute__((unused)), const float *c0 __attribute__((unused)),
    const float *c1 __attribute__((unused)), const float *c2 __attribute__((unused)))
{
}

bool gpu_postfx_init(int screen_width __attribute__((unused)), int screen_height __attribute__((unused)))
{
	return false;
}

void gpu_postfx_shutdown(void) {}

SDL_GPUTexture *gpu_render_target_create(int width __attribute__((unused)), int height __attribute__((unused)))
{
	return NULL;
}

bool gpu_set_render_target(SDL_GPUTexture *target __attribute__((unused)), int width __attribute__((unused)),
    int height __attribute__((unused)), bool clear __attribute__((unused)))
{
	return false;
}

void gpu_draw_set_blend_mode(int mode __attribute__((unused))) {}

int gpu_draw_get_blend_mode(void)
{
	return 0;
}

// Batched GPU text (sdl_gpu_text.c is not linked into the tests; the
// pure rasterizer sdl_text_glyph.c IS - it needs no stubs)
#include "sdl/sdl_gpu_text.h"

int gpu_text_draw_run(const char *text __attribute__((unused)), struct renderfont *font __attribute__((unused)),
    int r __attribute__((unused)), int g __attribute__((unused)), int b __attribute__((unused)),
    int sx __attribute__((unused)), int sy __attribute__((unused)), int clipsx __attribute__((unused)),
    int clipsy __attribute__((unused)), int clipex __attribute__((unused)), int clipey __attribute__((unused)),
    int x_offset __attribute__((unused)), int y_offset __attribute__((unused)))
{
	return 0;
}

void gpu_text_frame_begin(void) {}

void gpu_text_get_stats(int *runs, int *glyphs, int *fallbacks)
{
	if (runs) {
		*runs = 0;
	}
	if (glyphs) {
		*glyphs = 0;
	}
	if (fallbacks) {
		*fallbacks = 0;
	}
}

void gpu_text_reset(void) {}
