/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Keybind Panel UI — floating config panel opened by right-clicking
 * a hotbar slot. Lets players rebind the primary key, add/remove extra
 * bindings with per-bind cast mode and target overrides.
 *
 * The panel is context-aware: it only shows options that make sense
 * for the slot contents (e.g. self-only spells have no target/cast
 * options, items have no extra bindings).
 */

#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "gui/keybind_ui.h"
#include "client/client.h"
#include "game/game.h"

/* ── Constants ──────────────────────────────────────────────────────── */

#define KB_WIDTH 220
#define KB_PAD   8
#define KB_ROW   14 /* row height for small text + spacing */
#define KB_SEP   4 /* vertical gap between sections */
#define KB_BTN_H 12 /* clickable button text height */

/* highlight colors */
#define COL_TITLE   IRGB(31, 31, 20)
#define COL_LABEL   IRGB(24, 24, 24)
#define COL_VALUE   IRGB(31, 31, 31)
#define COL_BUTTON  IRGB(20, 28, 31)
#define COL_REMOVE  IRGB(31, 16, 16)
#define COL_CAPTURE IRGB(31, 31, 10)
#define COL_HEADER  IRGB(20, 20, 20)
#define COL_WARN    IRGB(31, 10, 10)
#define COL_CYCLE   IRGB(18, 28, 18)

/* ── State ──────────────────────────────────────────────────────────── */

static int kb_open;
static int kb_slot = -1;

/* capture: 0=none, 1=primary key, 100+idx=extra bind key at idx */
#define CAP_NONE         0
#define CAP_PRIMARY      1
#define CAP_EXTRA(i)     (100 + (i))
#define CAP_IS_EXTRA(c)  ((c) >= 100)
#define CAP_EXTRA_IDX(c) ((c) - 100)

static int kb_capture;
static int kb_capture_new; /* 1 = adding new bind, 0 = rebinding existing */

/* conflict warning */
static char kb_warn[80];
static uint32_t kb_warn_time;

/* ── Cast/target label tables ───────────────────────────────────────── */

static const char *cast_labels[] = {"Default", "Normal", "Quick", "Indicator"};
static const int cast_count = 4;

static const char *target_labels[] = {"Default", "Map", "Character", "Self"};

/* map HotbarTargetOverride enum → HOTBAR_VTGT_* flag */
static const int tgt_to_flag[] = {
    0, /* HOTBAR_TGT_DEFAULT — always valid */
    HOTBAR_VTGT_MAP, /* HOTBAR_TGT_MAP */
    HOTBAR_VTGT_CHR, /* HOTBAR_TGT_CHR */
    HOTBAR_VTGT_SELF, /* HOTBAR_TGT_SELF */
};

/* cycle to the next valid target override for a given spell */
static HotbarTargetOverride next_valid_target(HotbarTargetOverride cur, int valid_mask, int forward)
{
	for (int step = 0; step < 4; step++) {
		cur = (HotbarTargetOverride)(((int)cur + (forward ? 1 : 3)) % 4);
		/* Default is always valid; others must match the mask */
		if (cur == HOTBAR_TGT_DEFAULT || (tgt_to_flag[cur] & valid_mask)) {
			return cur;
		}
	}
	return HOTBAR_TGT_DEFAULT;
}

/* ── Layout ─────────────────────────────────────────────────────────── */

/* computed each frame */
static int px, py; /* panel top-left */
static int pw, ph; /* panel size */

/* row y-positions (computed by kb_layout, -1 = hidden) */
static int y_title;
static int y_primary;
static int y_primary_tgt; /* target override for primary key, -1 if hidden */
static int y_extra_header;
static int y_extra[HOTBAR_MAX_BINDS];
static int y_add;
static int y_cast_mode;
static int y_clear;

/* cached per-frame: does this slot support extra bindings / cast modes? */
static int kb_has_extras; /* 1 = show extra bindings section */
static int kb_has_cast; /* 1 = show cast mode options */
static int kb_valid_tgts; /* HOTBAR_VTGT_* bitmask */

static void kb_layout(void)
{
	const HotbarSlot *hs = hotbar_get(kb_slot);
	int extra_count = hs ? hs->extra_bind_count : 0;

	/* determine what options to show */
	if (hs && hs->type == HOTBAR_SPELL) {
		kb_has_cast = hotbar_spell_has_cast_modes(hs->action_slot);
		kb_valid_tgts = hotbar_spell_valid_targets(hs->action_slot);
		/* show extras if spell supports cast modes (targeted spells) */
		kb_has_extras = kb_has_cast;
	} else {
		kb_has_cast = 0;
		kb_valid_tgts = 0;
		kb_has_extras = 0;
	}

	pw = KB_WIDTH;

	/* build y positions top-down */
	int y = KB_PAD;

	y_title = y;
	y += KB_ROW + KB_SEP;

	y_primary = y;
	y += KB_ROW;

	/* primary target override — only for spells with multiple valid targets */
	if (kb_valid_tgts && (kb_valid_tgts & (kb_valid_tgts - 1)) != 0) {
		y_primary_tgt = y;
		y += KB_ROW;
	} else {
		y_primary_tgt = -1;
	}

	y += KB_SEP;

	if (kb_has_extras) {
		y_extra_header = y;
		y += KB_ROW;

		for (int i = 0; i < extra_count; i++) {
			y_extra[i] = y;
			y += KB_ROW;
		}

		if (extra_count < HOTBAR_MAX_BINDS) {
			y_add = y;
			y += KB_ROW;
		} else {
			y_add = -1;
		}

		y += KB_SEP;
		y_cast_mode = y;
		y += KB_ROW + KB_SEP;
	} else {
		y_extra_header = -1;
		y_add = -1;
		y_cast_mode = -1;
	}

	y_clear = y;
	y += KB_ROW;

	y += KB_PAD;
	ph = y;

	/* position: centered on hotbar slot, above it */
	int slot_x = butx(BUT_HOTBAR_BEG + kb_slot);
	int slot_y = buty(BUT_HOTBAR_BEG + kb_slot);

	px = slot_x - pw / 2;
	py = slot_y - ph - 24;

	/* clamp to screen */
	if (px < 2) {
		px = 2;
	}
	if (px + pw > XRES - 2) {
		px = XRES - pw - 2;
	}
	if (py < 2) {
		py = 2;
	}
}

/* ── Helpers ────────────────────────────────────────────────────────── */

static InputBinding *kb_primary_binding(void)
{
	char id[24];
	snprintf(id, sizeof(id), "hotbar.%d", kb_slot);
	return input_find_by_id(id);
}

static int in_rect(int mx, int my, int x, int y, int w, int h)
{
	return mx >= x && mx < x + w && my >= y && my < y + h;
}

/* ── Rendering ──────────────────────────────────────────────────────── */

void keybind_panel_display(void)
{
	if (!kb_open) {
		return;
	}

	const HotbarSlot *hs = hotbar_get(kb_slot);
	kb_layout();

	/* background */
	render_shaded_rect(px, py, px + pw, py + ph, 0, 175);

	int lx = px + KB_PAD;
	int rx = px + pw - KB_PAD;
	int cx = px + pw / 2;

	/* ── Title ────────────────────────────────────────────────────── */
	{
		const char *name = hotbar_slot_name(kb_slot);
		if (!name) {
			name = "(empty)";
		}
		render_text(cx, py + y_title, COL_TITLE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, name);
	}

	render_rect_alpha(lx, py + y_title + KB_ROW, rx, py + y_title + KB_ROW + 1, COL_HEADER, 100);

	/* ── Primary key ──────────────────────────────────────────────── */
	{
		InputBinding *b = kb_primary_binding();
		render_text(lx, py + y_primary, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Key:");

		if (kb_capture == CAP_PRIMARY) {
			render_rect_alpha(lx + 40, py + y_primary - 1, rx - 50, py + y_primary + KB_BTN_H + 1, COL_CAPTURE, 60);
			render_text(lx + 42, py + y_primary, COL_CAPTURE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Press a key...");
		} else if (b) {
			const char *kstr = input_key_to_string(b->key, b->modifiers);
			render_text(lx + 40, py + y_primary, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, kstr);
		}

		render_text(rx - 42, py + y_primary, COL_BUTTON, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "[Rebind]");
	}

	/* ── Primary target override ──────────────────────────────────── */
	if (y_primary_tgt >= 0 && hs && hs->type == HOTBAR_SPELL) {
		render_text(lx, py + y_primary_tgt, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Target:");
		char tbuf[32];
		snprintf(tbuf, sizeof(tbuf), "< %s >", target_labels[hs->primary_target]);
		render_text(lx + 56, py + y_primary_tgt, COL_CYCLE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, tbuf);
	}

	/* ── Extra bindings (only for targeted spells) ────────────────── */
	if (kb_has_extras) {
		render_rect_alpha(lx, py + y_extra_header - 2, rx, py + y_extra_header - 1, COL_HEADER, 100);
		render_text(cx, py + y_extra_header, COL_HEADER, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER,
		    "Extra Bindings");

		if (hs) {
			int show_tgt = (kb_valid_tgts & (kb_valid_tgts - 1)) != 0; /* more than one bit set */

			for (int i = 0; i < hs->extra_bind_count; i++) {
				int ry = py + y_extra[i];
				const HotbarBind *hb = &hs->extra_binds[i];

				/* key */
				if (kb_capture == CAP_EXTRA(i)) {
					render_rect_alpha(lx, ry - 1, lx + 70, ry + KB_BTN_H + 1, COL_CAPTURE, 60);
					render_text(lx + 2, ry, COL_CAPTURE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Press key...");
				} else {
					const char *kstr = input_key_to_string(hb->key, hb->modifiers);
					render_text(lx + 2, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, kstr);
				}

				/* cast mode cycle */
				if (kb_has_cast) {
					char cbuf[32];
					snprintf(cbuf, sizeof(cbuf), "<%s>", cast_labels[hb->cast_override]);
					render_text(lx + 76, ry, COL_CYCLE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, cbuf);
				}

				/* target cycle (only if spell has multiple valid targets) */
				if (show_tgt) {
					char tbuf[32];
					snprintf(tbuf, sizeof(tbuf), "<%s>", target_labels[hb->target_override]);
					render_text(lx + 140, ry, COL_CYCLE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, tbuf);
				}

				/* remove button */
				render_text(rx - 12, ry, COL_REMOVE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "X");
			}
		}

		/* add binding button */
		if (y_add >= 0) {
			if (kb_capture_new && CAP_IS_EXTRA(kb_capture)) {
				render_rect_alpha(lx, py + y_add - 1, lx + 120, py + y_add + KB_BTN_H + 1, COL_CAPTURE, 60);
				render_text(lx + 2, py + y_add, COL_CAPTURE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Press a key...");
			} else {
				render_text(lx, py + y_add, COL_BUTTON, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "[+ Add Binding]");
			}
		}

		/* global cast mode */
		render_rect_alpha(lx, py + y_cast_mode - 2, rx, py + y_cast_mode - 1, COL_HEADER, 100);
		render_text(lx, py + y_cast_mode, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Cast Mode:");

		static const char *mode_labels[] = {"Normal", "Quick", "Indicator"};
		int mode = hotbar_cast_mode();
		char mbuf[32];
		snprintf(mbuf, sizeof(mbuf), "< %s >", mode_labels[mode]);
		render_text(lx + 80, py + y_cast_mode, COL_CYCLE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, mbuf);
	}

	/* ── Bottom buttons ───────────────────────────────────────────── */
	render_rect_alpha(lx, py + y_clear - 2, rx, py + y_clear - 1, COL_HEADER, 100);
	render_text(lx, py + y_clear, COL_REMOVE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "[Clear Slot]");
	render_text(rx - 40, py + y_clear, COL_BUTTON, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "[Close]");

	/* ── Conflict warning ─────────────────────────────────────────── */
	if (kb_warn[0] && tick - kb_warn_time < TICKS * 3) {
		render_text(cx, py + ph + 2, COL_WARN, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, kb_warn);
	}
}

/* ── Open / Close ───────────────────────────────────────────────────── */

void keybind_panel_open(int hotbar_slot)
{
	if (kb_open && kb_slot == hotbar_slot) {
		keybind_panel_close();
		return;
	}
	kb_open = 1;
	kb_slot = hotbar_slot;
	kb_capture = CAP_NONE;
	kb_capture_new = 0;
	kb_warn[0] = '\0';
}

void keybind_panel_close(void)
{
	kb_open = 0;
	kb_capture = CAP_NONE;
	kb_capture_new = 0;
}

int keybind_panel_is_open(void)
{
	return kb_open;
}

/* ── Key capture ────────────────────────────────────────────────────── */

int keybind_panel_capturing(void)
{
	return kb_open && kb_capture != CAP_NONE;
}

void keybind_panel_cancel_capture(void)
{
	kb_capture = CAP_NONE;
	kb_capture_new = 0;
}

void keybind_panel_accept_key(SDL_Keycode key, Uint8 mods)
{
	if (kb_capture == CAP_PRIMARY) {
		char id[24];
		snprintf(id, sizeof(id), "hotbar.%d", kb_slot);
		InputBinding *conflict = input_find_conflict(key, mods, id);
		if (conflict) {
			snprintf(kb_warn, sizeof(kb_warn), "Unbound: %s", conflict->display_name);
			kb_warn_time = tick;
		}
		input_rebind(id, key, mods);
		save_options();
	} else if (CAP_IS_EXTRA(kb_capture)) {
		int idx = CAP_EXTRA_IDX(kb_capture);
		if (kb_capture_new) {
			hotbar_add_bind(kb_slot, key, mods, HOTBAR_CAST_DEFAULT, HOTBAR_TGT_DEFAULT);
		} else {
			hotbar_set_bind_key(kb_slot, idx, key, mods);
		}
		save_options();
	}

	kb_capture = CAP_NONE;
	kb_capture_new = 0;
}

/* ── Click handling ─────────────────────────────────────────────────── */

int keybind_panel_click(int mx, int my)
{
	if (!kb_open) {
		return 0;
	}

	kb_layout();

	if (!in_rect(mx, my, px, py, pw, ph)) {
		return 0;
	}

	const HotbarSlot *hs = hotbar_get(kb_slot);
	int lx = px + KB_PAD;
	int rx = px + pw - KB_PAD;

	/* ── Rebind primary key ────────────────────────────────────── */
	if (in_rect(mx, my, rx - 48, py + y_primary, 48, KB_BTN_H) ||
	    in_rect(mx, my, lx + 40, py + y_primary, 100, KB_BTN_H)) {
		kb_capture = CAP_PRIMARY;
		kb_capture_new = 0;
		return 1;
	}

	/* ── Primary target override cycle (left-click = forward) ──── */
	if (y_primary_tgt >= 0 && hs && hs->type == HOTBAR_SPELL &&
	    in_rect(mx, my, lx + 56, py + y_primary_tgt, 120, KB_BTN_H)) {
		HotbarTargetOverride next = next_valid_target(hs->primary_target, kb_valid_tgts, 1);
		hotbar_set_primary_target(kb_slot, next);
		save_options();
		return 1;
	}

	/* ── Extra bindings section (only if shown) ────────────────── */
	if (kb_has_extras && hs) {
		int show_tgt = (kb_valid_tgts & (kb_valid_tgts - 1)) != 0;

		for (int i = 0; i < hs->extra_bind_count; i++) {
			int ry = py + y_extra[i];

			/* click key = rebind */
			if (in_rect(mx, my, lx, ry, 74, KB_BTN_H)) {
				kb_capture = CAP_EXTRA(i);
				kb_capture_new = 0;
				return 1;
			}

			/* click cast label = cycle forward */
			if (kb_has_cast && in_rect(mx, my, lx + 76, ry, 60, KB_BTN_H)) {
				int next = ((int)hs->extra_binds[i].cast_override + 1) % cast_count;
				hotbar_set_bind_cast(kb_slot, i, (HotbarCastOverride)next);
				save_options();
				return 1;
			}

			/* click target label = cycle forward through valid targets */
			if (show_tgt && in_rect(mx, my, lx + 140, ry, 60, KB_BTN_H)) {
				HotbarTargetOverride next = next_valid_target(hs->extra_binds[i].target_override, kb_valid_tgts, 1);
				hotbar_set_bind_target(kb_slot, i, next);
				save_options();
				return 1;
			}

			/* click X = remove */
			if (in_rect(mx, my, rx - 16, ry, 16, KB_BTN_H)) {
				hotbar_remove_bind(kb_slot, i);
				save_options();
				return 1;
			}
		}

		/* add binding */
		if (y_add >= 0 && in_rect(mx, my, lx, py + y_add, 120, KB_BTN_H)) {
			int count = hs ? hs->extra_bind_count : 0;
			kb_capture = CAP_EXTRA(count);
			kb_capture_new = 1;
			return 1;
		}

		/* global cast mode cycle */
		if (y_cast_mode >= 0 && in_rect(mx, my, lx + 80, py + y_cast_mode, 100, KB_BTN_H)) {
			int next = (hotbar_cast_mode() + 1) % 3;
			hotbar_set_cast_mode(next);
			save_options();
			return 1;
		}
	}

	/* ── Clear slot ────────────────────────────────────────────── */
	if (in_rect(mx, my, lx, py + y_clear, 80, KB_BTN_H)) {
		hotbar_clear(kb_slot);
		save_options();
		keybind_panel_close();
		return 1;
	}

	/* ── Close button ──────────────────────────────────────────── */
	if (in_rect(mx, my, rx - 44, py + y_clear, 44, KB_BTN_H)) {
		keybind_panel_close();
		return 1;
	}

	return 1; /* consume click inside panel */
}

int keybind_panel_rclick(int mx, int my)
{
	if (!kb_open) {
		return 0;
	}

	kb_layout();

	if (!in_rect(mx, my, px, py, pw, ph)) {
		return 0;
	}

	const HotbarSlot *hs = hotbar_get(kb_slot);
	int lx = px + KB_PAD;

	/* ── Primary target override cycle (right-click = backward) ── */
	if (y_primary_tgt >= 0 && hs && hs->type == HOTBAR_SPELL &&
	    in_rect(mx, my, lx + 56, py + y_primary_tgt, 120, KB_BTN_H)) {
		HotbarTargetOverride prev = next_valid_target(hs->primary_target, kb_valid_tgts, 0);
		hotbar_set_primary_target(kb_slot, prev);
		save_options();
		return 1;
	}

	if (kb_has_extras && hs) {
		int show_tgt = (kb_valid_tgts & (kb_valid_tgts - 1)) != 0;

		for (int i = 0; i < hs->extra_bind_count; i++) {
			int ry = py + y_extra[i];

			/* right-click cast = cycle backward */
			if (kb_has_cast && in_rect(mx, my, lx + 76, ry, 60, KB_BTN_H)) {
				int prev = ((int)hs->extra_binds[i].cast_override + cast_count - 1) % cast_count;
				hotbar_set_bind_cast(kb_slot, i, (HotbarCastOverride)prev);
				save_options();
				return 1;
			}

			/* right-click target = cycle backward through valid targets */
			if (show_tgt && in_rect(mx, my, lx + 140, ry, 60, KB_BTN_H)) {
				HotbarTargetOverride prev = next_valid_target(hs->extra_binds[i].target_override, kb_valid_tgts, 0);
				hotbar_set_bind_target(kb_slot, i, prev);
				save_options();
				return 1;
			}
		}

		/* right-click global cast mode = cycle backward */
		if (y_cast_mode >= 0 && in_rect(mx, my, lx + 80, py + y_cast_mode, 100, KB_BTN_H)) {
			int prev = (hotbar_cast_mode() + 2) % 3;
			hotbar_set_cast_mode(prev);
			save_options();
			return 1;
		}
	}

	return 1; /* consume right-click inside panel */
}
