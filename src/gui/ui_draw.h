/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 */

#ifndef UI_DRAW_H
#define UI_DRAW_H

#include "gui/ui_tokens.h"

/* ui_button() states */
#define UI_BTN_REST     0
#define UI_BTN_HOVER    1
#define UI_BTN_PRESSED  2
#define UI_BTN_ACTIVE   3
#define UI_BTN_DISABLED 4

void ui_panel(int x1, int y1, int x2, int y2);
void ui_titlebar(int x1, int y1, int x2, const char *title);
void ui_row_hover(int x1, int y, int x2, int row_h);
void ui_section_header(int x, int y, int w, const char *label);
int ui_button(int x, int y, int w, int h, const char *label, int state);

#endif
