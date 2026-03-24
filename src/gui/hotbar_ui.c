/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Hotbar UI — renders the hotbar slots and handles drag-and-drop from
 * inventory (items) and spell book (spells) onto hotbar slots.
 */

#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "gui/spellbook_ui.h"
#include "client/client.h"
#include "game/game.h"

/* ── Hover tooltip state ──────────────────────────────────────────────── */

static int hb_hover_slot = -1;
static tick_t hb_hover_start;

/* ── Hotbar rendering ──────────────────────────────────────────────────── */

void hotbar_display(void)
{
	RenderFX fx;
	int hsel = -1; /* which hotbar slot is hovered */
	int cols = hotbar_visible_slots();
	int count = cols * hotbar_rows();

	/* detect hover */
	if (butsel >= BUT_HOTBAR_BEG && butsel <= BUT_HOTBAR_END) {
		hsel = butsel - BUT_HOTBAR_BEG;
	}

	if (count > HOTBAR_MAX_SLOTS) {
		count = HOTBAR_MAX_SLOTS;
	}

	for (int i = 0; i < count; i++) {
		int bi = BUT_HOTBAR_BEG + i;
		if (bi > BUT_HOTBAR_END) {
			break;
		}
		int x = butx(bi);
		int y = buty(bi);

		/* slot background (same as inventory slots) */
		render_sprite(opt_sprite(SPR_ITPAD), x, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);

		/* selection highlight on hover */
		if (i == hsel) {
			render_sprite(opt_sprite(SPR_ITSEL), x, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		}

		/* slot contents */
		uint32_t sprite = hotbar_slot_sprite(i);
		if (sprite) {
			const HotbarSlot *slot = hotbar_get(i);

			bzero(&fx, sizeof(fx));
			fx.sprite = sprite;
			fx.light = (i == hsel) ? RENDERFX_BRIGHT : RENDERFX_NORMAL_LIGHT;
			fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = fx.light;
			fx.align = RENDER_ALIGN_CENTER;
			fx.scale = 80; /* slightly smaller than full size to fit in slot */

			/* grey out item slots that are empty (out of stock) */
			if (slot && slot->type == HOTBAR_ITEM && slot->inv_index <= 0) {
				fx.sat = 20;
				fx.light = RENDERFX_NORMAL_LIGHT;
				fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = RENDERFX_NORMAL_LIGHT;
			}

			render_sprite_fx(&fx, x, y);
		}

		/* key label — show bound key in corner (toggle-able) */
		if (hotbar_show_hotkeys()) {
			InputBinding *b = NULL;
			for (int bi = 0; bi < input_binding_count(); bi++) {
				InputBinding *candidate = input_binding_at(bi);
				if (candidate && candidate->category == INPUT_CAT_HOTBAR && candidate->param == i) {
					b = candidate;
					break;
				}
			}
			if (b && b->key != SDLK_UNKNOWN) {
				char label[16];
				const char *kname = SDL_GetKeyName(b->key);
				if (kname && kname[0]) {
					snprintf(label, sizeof(label), "%s", kname);
				} else {
					snprintf(label, sizeof(label), "?");
				}
				render_text(x - 12, y - 14, whitecolor, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, label);
			}
		}

		/* slot name label below the slot (toggle-able) */
		if (hotbar_show_names()) {
			const char *name = hotbar_slot_name(i);
			if (name) {
				render_text(x, y + 16, whitecolor, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, name);
			}
		}
	}

	/* ── Hover tooltip ─────────────────────────────────────────────── */

	if (hsel != hb_hover_slot) {
		hb_hover_slot = hsel;
		hb_hover_start = tick + HOVER_DELAY;
	}

	if (hb_hover_slot >= 0 && hb_hover_slot < HOTBAR_MAX_SLOTS && tick > hb_hover_start) {
		const HotbarSlot *hs = hotbar_get(hb_hover_slot);
		int tx = butx(BUT_HOTBAR_BEG + hb_hover_slot);
		int ty = buty(BUT_HOTBAR_BEG + hb_hover_slot);

		if (hs && hs->type == HOTBAR_SPELL) {
			const char *name = get_action_text(hs->action_slot);
			const char *desc = get_action_desc(hs->action_slot);
			if (name || desc) {
				int text_h = 15;
				if (desc) {
					text_h += render_text_break_length(0, 0, 120, IRGB(31, 31, 31), 0, desc);
				}
				int sy = ty - text_h - 8;
				render_shaded_rect(tx - 64, sy, tx + 64, ty - 18, 0, 150);
				int text_y = sy + 4;
				if (name) {
					render_text(
					    tx, text_y, IRGB(31, 31, 31), RENDER_TEXT_BIG | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, name);
					text_y += 15;
				}
				if (desc) {
					render_text_break(tx - 60, text_y, tx + 60, IRGB(31, 31, 31), 0, desc);
				}
			}
		} else if (hs && hs->type == HOTBAR_ITEM && hs->inv_index > 0) {
			hover_render_for_slot(hs->inv_index, tx, ty - 18);
		}
	}

	/* spellbook toggle button on the right side of the hotbar */
	{
		int rx = butx(BUT_HOTBAR_BEG + cols - 1) + FDX + 8;
		int ry = buty(BUT_HOTBAR_BEG);
		bzero(&fx, sizeof(fx));
		fx.sprite = spellbook_is_open() ? 853U : 852U;
		fx.scale = 80;
		fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = RENDERFX_NORMAL_LIGHT;
		render_sprite_fx(&fx, rx, ry);
	}
}

/* ── Hotbar click handling ─────────────────────────────────────────────── */

int hotbar_click(int slot)
{
	if (slot < 0 || slot >= hotbar_visible_slots() * hotbar_rows()) {
		return 0;
	}

	/* dropping a spell from the spellbook onto this hotbar slot */
	if (spellbook_is_dragging()) {
		int action_slot = spellbook_dragging_slot();
		if (action_slot >= 0) {
			hotbar_assign_spell(slot, action_slot, 0, 0);
			save_options();
		}
		spellbook_cancel_drag();
		return 1;
	}

	/* if player is carrying an item on cursor, assign it to this slot */
	if (csprite) {
		hotbar_assign_item_by_type(slot, csprite);
		save_options();

		/* put the item back — prefer its original slot, fallback to first empty */
		int dest = -1;
		if (csprite_origin >= 30 && csprite_origin < _inventorysize && !item[csprite_origin]) {
			dest = csprite_origin;
		} else {
			for (int i = 30; i < _inventorysize; i++) {
				if (!item[i]) {
					dest = i;
					break;
				}
			}
		}
		if (dest >= 0) {
			cmd_swap(dest);
		}
		return 1;
	}

	/* nothing on cursor — activate the slot (use item / cast spell) */
	const HotbarSlot *hs = hotbar_get(slot);
	if (hs && hs->type != HOTBAR_EMPTY) {
		hotbar_activate(slot);
		return 1;
	}

	return 0;
}

/* spellbook toggle button hit test — called from gui_input on left-click */
int hotbar_toggle_hit(int mx, int my)
{
	int cols = hotbar_visible_slots();
	int rx = butx(BUT_HOTBAR_BEG + cols - 1) + FDX + 8;
	int ry = buty(BUT_HOTBAR_BEG);
	int dx = mx - rx;
	int dy = my - ry;
	if (dx >= -18 && dx <= 18 && dy >= -18 && dy <= 18) {
		spellbook_toggle();
		return 1;
	}
	return 0;
}
