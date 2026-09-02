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

/* ui_glyph_button() glyphs */
#define UI_GLYPH_CLOSE    0 /* X      */
#define UI_GLYPH_MINIMIZE 1 /* _      */
#define UI_GLYPH_RESTORE  2 /* square */

void ui_panel(int x1, int y1, int x2, int y2);
void ui_panel_light(int x1, int y1, int x2, int y2);
void ui_titlebar(int x1, int y1, int x2, const char *title);
void ui_row_hover(int x1, int y, int x2, int row_h);
void ui_section_header(int x, int y, int w, const char *label);
int ui_button(int x, int y, int w, int h, const char *label, int state);

/* Floating-window chrome: a compact titlebar with the title on the left and
 * square glyph buttons on the right. UI_WIN_TITLE_H tall. */
void ui_window_titlebar(int x1, int y1, int x2, const char *title, int collapsed);
void ui_glyph_button(int cx, int cy, int glyph, int hot);
void ui_resize_grip(int cx, int cy, int hot);
void ui_glyph_lock(int cx, int cy, int locked, int hot);

/* Item-slot backing cell (rounded socket) for grids that do not sit on the
 * classic bar art. */
void ui_slot_cell(int cx, int cy, int half, int hot, int selected);

/* Horizontal meter with an accent fill; `pct` is 0..100. */
void ui_meter_h(int x1, int y1, int x2, int y2, int pct, unsigned short color);

#endif
