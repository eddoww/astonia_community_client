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

#endif /* OPTIONS_UI_H */
