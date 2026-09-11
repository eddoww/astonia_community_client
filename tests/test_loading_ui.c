/*
 * Unit tests for the startup loading screen logic (src/gui/loading_ui.c):
 * server refusal parsing ("[retry=N]"), automatic-retry timing, notices and the
 * last-exit-reason used by the "not connected" screen. Rendering is stubbed.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "test.h"
#include "gui/loading_ui.h"

/* ---- stubs for everything loading_ui.c touches outside itself ---- */
int __xres = 800;
int __yres = 600;
int __uixres = 800;
int __uiyres = 600;
unsigned short textcolor = 1, redcolor = 2, blackcolor = 3;
char game_url[] = "https://ugaris.com";
static int text_calls;
static char last_text[512];

void note(const char *fmt, ...) { va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); fputc('\n', stderr); }
void render_rect(int a, int b, int c, int d, unsigned short e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void render_sprite(int a, int b, int c, int d, int e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void render_rounded_rect_alpha(int a, int b, int c, int d, int e, unsigned short f, unsigned char g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void render_rounded_rect_filled_alpha(int a, int b, int c, int d, int e, unsigned short f, unsigned char g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void render_circle_filled_alpha(int a, int b, int c, unsigned short d, unsigned char e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void render_line_alpha(int a, int b, int c, int d, unsigned short e, unsigned char f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
int render_text(int x, int y, unsigned short c, int flags, const char *t) { (void)x; (void)y; (void)c; (void)flags; text_calls++; snprintf(last_text, sizeof(last_text), "%s", t); return 0; }
int render_text_fmt(int x, int y, unsigned short c, int flags, const char *fmt, ...) { va_list a; va_start(a, fmt); vsnprintf(last_text, sizeof(last_text), fmt, a); va_end(a); (void)x; (void)y; (void)c; (void)flags; text_calls++; return 0; }
int render_text_length(int flags, const char *t) { (void)flags; return (int)strlen(t) * 6; } /* 6 px per char */
int sdl_is_shown(void) { return 1; }
void sdl_loop(void) {}
void sdl_render(void) {}

/* ---- tests ---- */
TEST(retry_tag_is_parsed_and_stripped)
{
	loading_step(LS_LOGIN);
	loading_server_exit("Another character is still in the world. [retry=2]");
	ASSERT_FALSE(loading_retry_due()); /* 2 s not elapsed */
	ASSERT_TRUE(strstr(loading_last_exit_reason(), "[retry=") == NULL);
	ASSERT_TRUE(strcmp(loading_last_exit_reason(), "Another character is still in the world.") == 0);
	SDL_Delay(2100);
	ASSERT_TRUE(loading_retry_due());
	loading_retry_begin();
	ASSERT_FALSE(loading_retry_due());
}

TEST(no_retry_tag_means_no_automatic_retry)
{
	loading_step(LS_LOGIN);
	loading_server_exit("Wrong username or password.");
	SDL_Delay(50);
	ASSERT_FALSE(loading_retry_due());
	ASSERT_TRUE(strcmp(loading_last_exit_reason(), "Wrong username or password.") == 0);
}

TEST(zero_or_garbage_retry_is_ignored)
{
	loading_step(LS_LOGIN);
	loading_server_exit("Nope. [retry=0]");
	ASSERT_FALSE(loading_retry_due());
	loading_server_exit("Nope. [retry=abc]");
	ASSERT_FALSE(loading_retry_due());
}

TEST(error_text_is_rendered_on_the_loading_screen)
{
	loading_step(LS_CONNECT);
	loading_server_exit("The server is restarting. Please try again in a few minutes. [retry=60]");
	text_calls = 0;
	loading_display();
	ASSERT_TRUE(text_calls > 0);
	ASSERT_TRUE(strstr(last_text, "Retrying automatically in") != NULL);
}

TEST(notice_shows_and_clears)
{
	loading_retry_begin(); /* clears any error from earlier tests */
	loading_step(LS_CONNECT);
	loading_notice("Could not reach the server at x - retrying (attempt 2).");
	text_calls = 0;
	loading_display();
	ASSERT_TRUE(text_calls > 0);
	loading_notice(NULL);
	loading_notice(NULL); /* idempotent */
}

TEST(finished_screen_ignores_errors_but_keeps_last_reason)
{
	loading_finish();
	ASSERT_FALSE(loading_active());
	loading_server_exit("idle too long [retry=5]");
	ASSERT_FALSE(loading_retry_due());
	ASSERT_TRUE(strcmp(loading_last_exit_reason(), "idle too long") == 0);
}

TEST_MAIN(retry_tag_is_parsed_and_stripped(); no_retry_tag_means_no_automatic_retry();
    zero_or_garbage_retry_is_ignored(); error_text_is_rendered_on_the_loading_screen(); notice_shows_and_clears();
    finished_screen_ignores_errors_but_keeps_last_reason();)
