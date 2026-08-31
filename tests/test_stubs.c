/*
 * Test Stubs - Minimal implementations of game functions for unit testing
 *
 * These stubs allow SDL code to link without the full game engine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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

void gui_sdl_keyproc(SDL_Keycode key __attribute__((unused)))
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
#include "sdl/sdl_gpu_batch.h"

bool use_gpu_rendering = false;
bool gpu_rendering_requested = false;
SDL_GPUDevice *sdlgpu = NULL;

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

bool gpu_draw_prim_is_available(void)
{
	return false;
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

void gpu_draw_rect(float x __attribute__((unused)), float y __attribute__((unused)),
    float w __attribute__((unused)), float h __attribute__((unused)), float r __attribute__((unused)),
    float g __attribute__((unused)), float b __attribute__((unused)), float a __attribute__((unused)))
{
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

bool gpu_batch_init(int screen_width __attribute__((unused)), int screen_height __attribute__((unused)))
{
	return false;
}

void gpu_batch_shutdown(void) {}

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
