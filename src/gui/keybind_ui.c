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
#include "gui/ui_draw.h"
#include "client/client.h"
#include "game/game.h"

/* ── Constants ──────────────────────────────────────────────────────── */

#define KB_WIDTH 220
#define KB_PAD   8
#define KB_ROW   14 /* row height for small text + spacing */
#define KB_SEP   4 /* vertical gap between sections */
#define KB_BTN_H 12 /* clickable button text height */

/* highlight colors with no shared-token equivalent */
#define COL_REMOVE  IRGB(28, 14, 12)
#define COL_CAPTURE IRGB(28, 26, 16)
#define COL_WARN    IRGB(28, 12, 10)
#define COL_CYCLE   IRGB(22, 24, 18)

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

static const char *cast_labels[] = {"Default", "Normal", "Quick", "Indicator", "Smart"};
static const int cast_count = 5;

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

typedef struct {
	int x, y, w, h;
} KbRect;

/* Clickable regions in absolute screen coordinates, computed once per
 * frame by kb_layout() and shared by keybind_panel_display(),
 * keybind_panel_click() and keybind_panel_rclick() so hover feedback,
 * drawing and hit-testing cannot drift apart. A zeroed rect (w == 0)
 * means the region is not present. */
static struct {
	KbRect rebind_btn; /* "[Rebind]" */
	KbRect primary_key; /* key text next to "Key:" (also starts capture) */
	KbRect undo; /* "[Undo]" (hit-tested even when hidden, as before) */
	int undo_visible;
	KbRect primary_tgt; /* target cycle for the primary key */
	KbRect extra_key[HOTBAR_MAX_BINDS];
	KbRect extra_cast[HOTBAR_MAX_BINDS];
	KbRect extra_tgt[HOTBAR_MAX_BINDS];
	KbRect extra_remove[HOTBAR_MAX_BINDS];
	int extra_count; /* extras with valid rects (0 unless kb_has_extras) */
	int extra_show_tgt; /* spell has more than one valid target */
	KbRect add; /* "[+ Add Binding]" */
	KbRect cast_mode; /* global cast mode cycle */
	KbRect clear; /* "[Clear Slot]" */
	KbRect close; /* "[Close]" */
} kb_r;

static InputBinding *kb_primary_binding(void);

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
	if (px + pw > UIXRES - 2) {
		px = UIXRES - pw - 2;
	}
	if (py < 2) {
		py = 2;
	}

	/* clickable-region table (see kb_r above) */
	int lx = px + KB_PAD;
	int rx = px + pw - KB_PAD;

	memset(&kb_r, 0, sizeof(kb_r));
	kb_r.rebind_btn = (KbRect){rx - 48, py + y_primary, 48, KB_BTN_H};
	kb_r.primary_key = (KbRect){lx + 40, py + y_primary, 100, KB_BTN_H};
	kb_r.undo = (KbRect){lx + 40, py + y_primary + KB_ROW, 40, KB_BTN_H};
	{
		InputBinding *b = kb_primary_binding();
		kb_r.undo_visible = b && (b->key != b->default_key || b->modifiers != b->default_modifiers);
	}
	if (y_primary_tgt >= 0 && hs && hs->type == HOTBAR_SPELL) {
		kb_r.primary_tgt = (KbRect){lx + 56, py + y_primary_tgt, 120, KB_BTN_H};
	}
	if (kb_has_extras && hs) {
		kb_r.extra_show_tgt = (kb_valid_tgts & (kb_valid_tgts - 1)) != 0;
		kb_r.extra_count = extra_count;
		for (int i = 0; i < extra_count; i++) {
			int ry = py + y_extra[i];
			kb_r.extra_key[i] = (KbRect){lx, ry, 74, KB_BTN_H};
			if (kb_has_cast) {
				kb_r.extra_cast[i] = (KbRect){lx + 76, ry, 60, KB_BTN_H};
			}
			if (kb_r.extra_show_tgt) {
				kb_r.extra_tgt[i] = (KbRect){lx + 140, ry, 60, KB_BTN_H};
			}
			kb_r.extra_remove[i] = (KbRect){rx - 16, ry, 16, KB_BTN_H};
		}
		if (y_add >= 0) {
			kb_r.add = (KbRect){lx, py + y_add, 120, KB_BTN_H};
		}
		kb_r.cast_mode = (KbRect){lx + 80, py + y_cast_mode, 100, KB_BTN_H};
	}
	kb_r.clear = (KbRect){lx, py + y_clear, 80, KB_BTN_H};
	kb_r.close = (KbRect){rx - 44, py + y_clear, 44, KB_BTN_H};
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

static int kb_hit(const KbRect *r, int mx, int my)
{
	return r->w > 0 && in_rect(mx, my, r->x, r->y, r->w, r->h);
}

/* soft hover wash behind a clickable region */
static void kb_hover(const KbRect *r)
{
	if (kb_hit(r, mousex, mousey)) {
		ui_row_hover(r->x - 2, r->y - 1, r->x + r->w + 2, r->h + 2);
	}
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
	ui_panel(px, py, px + pw, py + ph);

	int lx = px + KB_PAD;
	int rx = px + pw - KB_PAD;
	int cx = px + pw / 2;

	/* ── Title ────────────────────────────────────────────────────── */
	{
		const char *name = hotbar_slot_name(kb_slot);
		if (!name) {
			name = "(empty)";
		}
		render_text(cx, py + y_title, UI_TEXT_TITLE, UI_FONT_CENTER, name);
	}

	render_rect_alpha(lx, py + y_title + KB_ROW, rx, py + y_title + KB_ROW + 1, UI_BORDER, UI_A_RULE);

	/* ── Primary key ──────────────────────────────────────────────── */
	{
		InputBinding *b = kb_primary_binding();
		render_text(lx, py + y_primary, UI_TEXT_LABEL, UI_FONT_BODY, "Key:");

		if (kb_capture == CAP_PRIMARY) {
			render_rect_alpha(lx + 40, py + y_primary - 1, rx - 50, py + y_primary + KB_BTN_H + 1, COL_CAPTURE, 60);
			render_text(lx + 42, py + y_primary, COL_CAPTURE, UI_FONT_BODY, "Press a key...");
		} else if (b) {
			kb_hover(&kb_r.primary_key);
			const char *kstr = input_key_to_string(b->key, b->modifiers);
			render_text(lx + 40, py + y_primary, UI_TEXT, UI_FONT_BODY, kstr);
		}

		kb_hover(&kb_r.rebind_btn);
		render_text(rx - 42, py + y_primary, UI_TEXT_LABEL, UI_FONT_BODY, "[Rebind]");

		if (kb_r.undo_visible) {
			kb_hover(&kb_r.undo);
			render_text(lx + 40, py + y_primary + KB_ROW, UI_TEXT_LABEL, UI_FONT_BODY, "[Undo]");
		}
	}

	/* ── Primary target override ──────────────────────────────────── */
	if (y_primary_tgt >= 0 && hs && hs->type == HOTBAR_SPELL) {
		render_text(lx, py + y_primary_tgt, UI_TEXT_LABEL, UI_FONT_BODY, "Target:");
		char tbuf[32];
		snprintf(tbuf, sizeof(tbuf), "< %s >", target_labels[hs->primary_target]);
		kb_hover(&kb_r.primary_tgt);
		render_text(lx + 56, py + y_primary_tgt, COL_CYCLE, UI_FONT_BODY, tbuf);
	}

	/* ── Extra bindings (only for targeted spells) ────────────────── */
	if (kb_has_extras) {
		render_rect_alpha(lx, py + y_extra_header - 2, rx, py + y_extra_header - 1, UI_BORDER, UI_A_RULE);
		render_text(cx, py + y_extra_header, UI_TEXT_MUTED, UI_FONT_CENTER, "Extra Bindings");

		if (hs) {
			for (int i = 0; i < hs->extra_bind_count; i++) {
				int ry = py + y_extra[i];
				const HotbarBind *hb = &hs->extra_binds[i];

				/* key */
				if (kb_capture == CAP_EXTRA(i)) {
					render_rect_alpha(lx, ry - 1, lx + 70, ry + KB_BTN_H + 1, COL_CAPTURE, 60);
					render_text(lx + 2, ry, COL_CAPTURE, UI_FONT_BODY, "Press key...");
				} else {
					kb_hover(&kb_r.extra_key[i]);
					const char *kstr = input_key_to_string(hb->key, hb->modifiers);
					render_text(lx + 2, ry, UI_TEXT, UI_FONT_BODY, kstr);
				}

				/* cast mode cycle */
				if (kb_has_cast) {
					char cbuf[32];
					snprintf(cbuf, sizeof(cbuf), "<%s>", cast_labels[hb->cast_override]);
					kb_hover(&kb_r.extra_cast[i]);
					render_text(lx + 76, ry, COL_CYCLE, UI_FONT_BODY, cbuf);
				}

				/* target cycle (only if spell has multiple valid targets) */
				if (kb_r.extra_show_tgt) {
					char tbuf[32];
					snprintf(tbuf, sizeof(tbuf), "<%s>", target_labels[hb->target_override]);
					kb_hover(&kb_r.extra_tgt[i]);
					render_text(lx + 140, ry, COL_CYCLE, UI_FONT_BODY, tbuf);
				}

				/* remove button */
				kb_hover(&kb_r.extra_remove[i]);
				render_text(rx - 12, ry, COL_REMOVE, UI_FONT_BODY, "X");
			}
		}

		/* add binding button */
		if (y_add >= 0) {
			if (kb_capture_new && CAP_IS_EXTRA(kb_capture)) {
				render_rect_alpha(lx, py + y_add - 1, lx + 120, py + y_add + KB_BTN_H + 1, COL_CAPTURE, 60);
				render_text(lx + 2, py + y_add, COL_CAPTURE, UI_FONT_BODY, "Press a key...");
			} else {
				kb_hover(&kb_r.add);
				render_text(lx, py + y_add, UI_TEXT_LABEL, UI_FONT_BODY, "[+ Add Binding]");
			}
		}

		/* global cast mode */
		render_rect_alpha(lx, py + y_cast_mode - 2, rx, py + y_cast_mode - 1, UI_BORDER, UI_A_RULE);
		render_text(lx, py + y_cast_mode, UI_TEXT_LABEL, UI_FONT_BODY, "Cast Mode:");

		static const char *mode_labels[] = {"Normal", "Quick", "Indicator", "Smart"};
		int mode = hotbar_cast_mode();
		char mbuf[32];
		snprintf(mbuf, sizeof(mbuf), "< %s >", mode_labels[mode]);
		kb_hover(&kb_r.cast_mode);
		render_text(lx + 80, py + y_cast_mode, COL_CYCLE, UI_FONT_BODY, mbuf);
	}

	/* ── Bottom buttons ───────────────────────────────────────────── */
	render_rect_alpha(lx, py + y_clear - 2, rx, py + y_clear - 1, UI_BORDER, UI_A_RULE);
	kb_hover(&kb_r.clear);
	render_text(lx, py + y_clear, COL_REMOVE, UI_FONT_BODY, "[Clear Slot]");
	kb_hover(&kb_r.close);
	render_text(rx - 40, py + y_clear, UI_TEXT_LABEL, UI_FONT_BODY, "[Close]");

	/* ── Conflict warning ─────────────────────────────────────────── */
	if (kb_warn[0] && tick - kb_warn_time < TICKS * 3) {
		render_text(cx, py + ph + 2, COL_WARN, UI_FONT_CENTER, kb_warn);
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

	/* ── Rebind primary key ────────────────────────────────────── */
	if (kb_hit(&kb_r.rebind_btn, mx, my) || kb_hit(&kb_r.primary_key, mx, my)) {
		kb_capture = CAP_PRIMARY;
		kb_capture_new = 0;
		return 1;
	}

	/* ── Undo last rebind ──────────────────────────────────────── */
	if (kb_hit(&kb_r.undo, mx, my)) {
		if (input_undo_rebind() == 0) {
			save_options();
		}
		return 1;
	}

	/* ── Primary target override cycle (left-click = forward) ──── */
	if (kb_hit(&kb_r.primary_tgt, mx, my) && hs) {
		HotbarTargetOverride next = next_valid_target(hs->primary_target, kb_valid_tgts, 1);
		hotbar_set_primary_target(kb_slot, next);
		save_options();
		return 1;
	}

	/* ── Extra bindings section (only if shown) ────────────────── */
	if (kb_has_extras && hs) {
		for (int i = 0; i < kb_r.extra_count; i++) {
			/* click key = rebind */
			if (kb_hit(&kb_r.extra_key[i], mx, my)) {
				kb_capture = CAP_EXTRA(i);
				kb_capture_new = 0;
				return 1;
			}

			/* click cast label = cycle forward */
			if (kb_hit(&kb_r.extra_cast[i], mx, my)) {
				int next = ((int)hs->extra_binds[i].cast_override + 1) % cast_count;
				hotbar_set_bind_cast(kb_slot, i, (HotbarCastOverride)next);
				save_options();
				return 1;
			}

			/* click target label = cycle forward through valid targets */
			if (kb_hit(&kb_r.extra_tgt[i], mx, my)) {
				HotbarTargetOverride next = next_valid_target(hs->extra_binds[i].target_override, kb_valid_tgts, 1);
				hotbar_set_bind_target(kb_slot, i, next);
				save_options();
				return 1;
			}

			/* click X = remove */
			if (kb_hit(&kb_r.extra_remove[i], mx, my)) {
				hotbar_remove_bind(kb_slot, i);
				save_options();
				return 1;
			}
		}

		/* add binding */
		if (kb_hit(&kb_r.add, mx, my)) {
			kb_capture = CAP_EXTRA(kb_r.extra_count);
			kb_capture_new = 1;
			return 1;
		}

		/* global cast mode cycle */
		if (kb_hit(&kb_r.cast_mode, mx, my)) {
			int next = (hotbar_cast_mode() + 1) % 4;
			hotbar_set_cast_mode(next);
			save_options();
			return 1;
		}
	}

	/* ── Clear slot ────────────────────────────────────────────── */
	if (kb_hit(&kb_r.clear, mx, my)) {
		hotbar_clear(kb_slot);
		save_options();
		keybind_panel_close();
		return 1;
	}

	/* ── Close button ──────────────────────────────────────────── */
	if (kb_hit(&kb_r.close, mx, my)) {
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

	/* ── Primary target override cycle (right-click = backward) ── */
	if (kb_hit(&kb_r.primary_tgt, mx, my) && hs) {
		HotbarTargetOverride prev = next_valid_target(hs->primary_target, kb_valid_tgts, 0);
		hotbar_set_primary_target(kb_slot, prev);
		save_options();
		return 1;
	}

	if (kb_has_extras && hs) {
		for (int i = 0; i < kb_r.extra_count; i++) {
			/* right-click cast = cycle backward */
			if (kb_hit(&kb_r.extra_cast[i], mx, my)) {
				int prev = ((int)hs->extra_binds[i].cast_override + cast_count - 1) % cast_count;
				hotbar_set_bind_cast(kb_slot, i, (HotbarCastOverride)prev);
				save_options();
				return 1;
			}

			/* right-click target = cycle backward through valid targets */
			if (kb_hit(&kb_r.extra_tgt[i], mx, my)) {
				HotbarTargetOverride prev = next_valid_target(hs->extra_binds[i].target_override, kb_valid_tgts, 0);
				hotbar_set_bind_target(kb_slot, i, prev);
				save_options();
				return 1;
			}
		}

		/* right-click global cast mode = cycle backward */
		if (kb_hit(&kb_r.cast_mode, mx, my)) {
			int prev = (hotbar_cast_mode() + 3) % 4;
			hotbar_set_cast_mode(prev);
			save_options();
			return 1;
		}
	}

	return 1; /* consume right-click inside panel */
}
