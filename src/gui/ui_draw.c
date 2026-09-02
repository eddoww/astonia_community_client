/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Shared UI drawing helpers implementing the design language of the
 * Options window: rounded panels with a vertical gradient, amber
 * accent, soft row hover washes. All colors, alphas and dimensions
 * come from the shared token set in ui_tokens.h.
 */

#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/ui_draw.h"
#include "game/game.h"

/* Rounded panel: dark fill, vertical gradient on the upper half,
 * rounded border. Same recipe as the Options window panel. */
void ui_panel(int x1, int y1, int x2, int y2)
{
	render_rounded_rect_filled_alpha(x1, y1, x2, y2, UI_R_PANEL, UI_BG_BASE, UI_A_PANEL);
	render_gradient_rect_v(x1 + 1, y1 + 1, x2 - 1, y1 + (y2 - y1) / 2, UI_BG_RAISED, UI_BG_BASE, UI_A_PANEL_GRAD);
	render_rounded_rect_alpha(x1, y1, x2, y2, UI_R_PANEL, UI_BORDER, UI_A_PANEL_GRAD);
}

/* Centered window title plus the separator rule below it. Reserves
 * UI_TITLE_H pixels of panel height. */
void ui_titlebar(int x1, int y1, int x2, const char *title)
{
	render_text((x1 + x2) / 2, y1 + UI_PAD, UI_TEXT_TITLE, UI_FONT_TITLE | RENDER_ALIGN_CENTER, title);
	render_rect_alpha(x1 + 1, y1 + UI_TITLE_H - 2, x2 - 1, y1 + UI_TITLE_H - 1, UI_BORDER, UI_A_RULE);
}

/* Soft highlight wash behind the row under the mouse cursor. */
void ui_row_hover(int x1, int y, int x2, int row_h)
{
	render_rounded_rect_filled_alpha(x1, y, x2, y + row_h, UI_R_ROW, UI_BG_ROW_HOVER, UI_A_ROW_HOVER);
}

/* Accent-colored section label with a gradient rule filling the rest
 * of the width. Same recipe as the Options window section headers. */
void ui_section_header(int x, int y, int w, const char *label)
{
	int lw = render_text_length(RENDER_TEXT_SMALL, label);

	render_text(x, y, UI_ACCENT, UI_FONT_BODY, label);
	render_gradient_rect_h(x + lw + 6, y + 7, x + w, y + 8, UI_ACCENT, UI_BG_BASE, 170);
}

/* Button with the shared five-state look (see UI_BTN_*). Draw-only:
 * hit testing stays with the caller. Returns 0. */
int ui_button(int x, int y, int w, int h, const char *label, int state)
{
	unsigned short fill = UI_BG_BASE, border = UI_BORDER, text = UI_TEXT_LABEL;
	unsigned char fill_a = UI_A_CONTROL, border_a = UI_A_BORDER_REST;
	int off = 0;

	switch (state) {
	case UI_BTN_HOVER:
		fill = UI_BG_ROW_HOVER;
		border = UI_ACCENT;
		border_a = UI_A_BORDER_HOV;
		text = UI_TEXT;
		break;
	case UI_BTN_PRESSED:
		fill = UI_BG_SUNKEN;
		border_a = UI_A_BORDER_HOV;
		text = UI_TEXT_MUTED;
		off = 1;
		break;
	case UI_BTN_ACTIVE:
		fill = UI_BG_ROW_ACTIVE;
		border = UI_ACCENT;
		border_a = 255;
		text = UI_TEXT;
		break;
	case UI_BTN_DISABLED:
		border_a = UI_A_RULE;
		text = UI_TEXT_DISABLED;
		break;
	default:
		break;
	}

	render_rounded_rect_filled_alpha(x, y, x + w, y + h, UI_R_BUTTON, fill, fill_a);
	render_rounded_rect_alpha(x, y, x + w, y + h, UI_R_BUTTON, border, border_a);
	if (state == UI_BTN_HOVER) {
		/* accent bar at the left inset (escape-menu idiom) */
		render_rect_alpha(x + 2, y + 2, x + 4, y + h - 2, UI_ACCENT, 230);
	}
	if (state == UI_BTN_ACTIVE) {
		render_rect_alpha(x + 2, y + h - 2, x + w - 2, y + h, UI_ACCENT, 255);
	}
	render_text(x + w / 2 + off, y + (h - 10) / 2 + off, text, UI_FONT_CENTER, label);
	return 0;
}

/* Translucent panel for small always-on HUD widgets (buffs, speed): same
 * shape as ui_panel() but see-through, so it sits on the world without
 * reading as a heavy dialog. */
void ui_panel_light(int x1, int y1, int x2, int y2)
{
	render_rounded_rect_filled_alpha(x1, y1, x2, y2, UI_R_PANEL, UI_BG_BASE, UI_A_PANEL_HUD);
	render_rounded_rect_alpha(x1, y1, x2, y2, UI_R_PANEL, UI_BORDER, UI_A_BORDER_REST);
}

/* Compact titlebar of a floating panel window: left-aligned title, a
 * gradient wash and the rule that separates it from the body. The glyph
 * buttons are drawn separately (they need their own hover state). */
void ui_window_titlebar(int x1, int y1, int x2, const char *title, int collapsed)
{
	render_gradient_rect_v(x1 + 1, y1 + 1, x2 - 1, y1 + UI_WIN_TITLE_H - 1, UI_BG_RAISED, UI_BG_BASE, UI_A_PANEL_GRAD);
	if (!collapsed) {
		render_rect_alpha(x1 + 1, y1 + UI_WIN_TITLE_H - 1, x2 - 1, y1 + UI_WIN_TITLE_H, UI_BORDER, UI_A_RULE);
	}
	render_rect_alpha(x1 + UI_WIN_PAD - 2, y1 + 4, x1 + UI_WIN_PAD, y1 + UI_WIN_TITLE_H - 4, UI_ACCENT, 210);
	render_text(x1 + UI_WIN_PAD + 4, y1 + 3, UI_TEXT_TITLE, UI_FONT_TITLE, title);
}

/* Square titlebar button drawn around (cx,cy) carrying one of the
 * UI_GLYPH_* marks. */
void ui_glyph_button(int cx, int cy, int glyph, int hot)
{
	int h = UI_WIN_GLYPH / 2;
	unsigned short mark = hot ? UI_TEXT : UI_TEXT_LABEL;

	if (hot) {
		render_rounded_rect_filled_alpha(cx - h, cy - h, cx + h, cy + h, UI_R_CHIP, UI_BG_ROW_HOVER, UI_A_CONTROL);
		render_rounded_rect_alpha(cx - h, cy - h, cx + h, cy + h, UI_R_CHIP, UI_ACCENT, UI_A_BORDER_HOV);
	}
	switch (glyph) {
	case UI_GLYPH_CLOSE:
		render_line(cx - 3, cy - 3, cx + 3, cy + 3, hot ? UI_TEXT_ERROR : mark);
		render_line(cx + 3, cy - 3, cx - 3, cy + 3, hot ? UI_TEXT_ERROR : mark);
		break;
	case UI_GLYPH_MINIMIZE:
		render_rect_alpha(cx - 3, cy + 2, cx + 4, cy + 3, mark, 255);
		break;
	case UI_GLYPH_RESTORE:
	default:
		render_rect_outline_alpha(cx - 3, cy - 3, cx + 4, cy + 4, mark, 255);
		break;
	}
}

/* Titlebar padlock: solid amber body when locked, faint outline when free.
 * (cx,cy) is the glyph centre; hover gets the shared chip backing. */
void ui_glyph_lock(int cx, int cy, int locked, int hot)
{
	unsigned short col = locked ? UI_ACCENT : (hot ? UI_TEXT : UI_TEXT_LABEL);
	unsigned char a = locked ? 255 : (hot ? 255 : 170);

	if (hot) {
		int h = UI_WIN_GLYPH / 2;

		render_rounded_rect_filled_alpha(cx - h, cy - h, cx + h, cy + h, UI_R_CHIP, UI_BG_ROW_HOVER, UI_A_CONTROL);
		render_rounded_rect_alpha(cx - h, cy - h, cx + h, cy + h, UI_R_CHIP, UI_ACCENT, UI_A_BORDER_HOV);
	}
	/* shackle */
	render_rect_alpha(cx - 2, cy - 4, cx - 1, cy - 1, col, a);
	render_rect_alpha(cx + 2, cy - 4, cx + 3, cy - 1, col, a);
	render_rect_alpha(cx - 2, cy - 5, cx + 3, cy - 4, col, a);
	/* body */
	if (locked) {
		render_rect_alpha(cx - 3, cy - 1, cx + 4, cy + 4, col, a);
	} else {
		render_rect_outline_alpha(cx - 3, cy - 1, cx + 4, cy + 4, col, a);
	}
}

/* Three diagonal ticks in the bottom-right corner, the usual "drag me to
 * resize" affordance. (cx,cy) is the grip's centre. */
void ui_resize_grip(int cx, int cy, int hot)
{
	unsigned short col = hot ? UI_ACCENT : UI_BORDER_STRONG;
	int h = UI_WIN_GRIP / 2;
	int i;

	for (i = 0; i < 3; i++) {
		int o = i * 3;
		render_line_alpha(cx + h - o, cy + h, cx + h, cy + h - o, col, hot ? 255 : 190);
	}
}

/* Rounded socket behind an item slot, for grids that no longer sit in the
 * bar art's recess. `half` is half the cell side. */
void ui_slot_cell(int cx, int cy, int half, int hot, int selected)
{
	render_rounded_rect_filled_alpha(cx - half, cy - half, cx + half, cy + half, UI_R_ROW, UI_BG_SUNKEN, UI_A_SOCKET);
	if (selected) {
		render_rounded_rect_alpha(cx - half, cy - half, cx + half, cy + half, UI_R_ROW, UI_ACCENT, 255);
	} else if (hot) {
		render_rounded_rect_alpha(cx - half, cy - half, cx + half, cy + half, UI_R_ROW, UI_ACCENT, UI_A_BORDER_HOV);
	} else {
		render_rounded_rect_alpha(cx - half, cy - half, cx + half, cy + half, UI_R_ROW, UI_BORDER, UI_A_BORDER_REST);
	}
}

/* Horizontal meter: sunken trough with a filled bar. pct is clamped 0..100. */
void ui_meter_h(int x1, int y1, int x2, int y2, int pct, unsigned short color)
{
	int w = x2 - x1;

	if (pct < 0) {
		pct = 0;
	}
	if (pct > 100) {
		pct = 100;
	}
	render_rounded_rect_filled_alpha(x1, y1, x2, y2, UI_R_CHIP, UI_BG_SUNKEN, UI_A_CONTROL);
	if (pct > 0) {
		render_rounded_rect_filled_alpha(x1 + 1, y1 + 1, x1 + 1 + (w - 2) * pct / 100, y2 - 1, UI_R_CHIP, color, 235);
	}
	render_rounded_rect_alpha(x1, y1, x2, y2, UI_R_CHIP, UI_BORDER, UI_A_BORDER_REST);
}
