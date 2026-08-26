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
