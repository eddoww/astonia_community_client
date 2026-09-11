/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 */

#ifndef OPTIONS_UI_H
#define OPTIONS_UI_H

void options_display(void);
int options_click(int mx, int my);
int options_scroll(int delta);
int options_is_open(void);
void options_open(void);
void options_close(void);
/* Apply a window mode (0 windowed / 1 borderless / 2 exclusive) and re-derive
 * the canvas. Used by the Options click and the startup restore of the saved
 * mode (main.c: saved_window_mode). */
void options_apply_window_mode(int mode);

#endif /* OPTIONS_UI_H */
