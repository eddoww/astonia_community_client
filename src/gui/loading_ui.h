/*
 * Startup loading screen: the first thing the player sees after the window
 * opens. Shows named steps with check marks and a progress bar, and hands
 * over to the game only when the world is ready.
 */
#ifndef LOADING_UI_H
#define LOADING_UI_H

enum loading_step { LS_GRAPHICS = 0, LS_SOUND, LS_MODS, LS_CONNECT, LS_LOGIN, LS_WORLD, LS_COUNT };

void loading_step(int step); /* mark step active (previous ones done) */
void loading_progress(int done, int total); /* progress of the active step (0/0 = unknown) */
void loading_detail(const char *text); /* optional sub-text under the active step (NULL = none) */
void loading_finish(void); /* everything done: game takes over */
int loading_active(void); /* 1 while the startup screen should be shown */
void loading_display(void); /* draw the screen (caller presents) */
void loading_present(void); /* draw + present immediately (use between init steps) */

/* The server refused/ended the session while the loading screen is up (SV_EXIT):
 * show the reason on the screen. A trailing "[retry=<seconds>]" in the text starts
 * a countdown after which the client reconnects and logs in again by itself. */
void loading_server_exit(const char *reason);
int loading_retry_due(void); /* 1 when the countdown has elapsed (caller reconnects) */
void loading_retry_begin(void); /* clear the error and show "Connecting" again */

#endif
