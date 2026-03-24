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

/* ── Hotbar rendering ──────────────────────────────────────────────────── */

void hotbar_display(void)
{
	RenderFX fx;
	int hsel = -1; /* which hotbar slot is hovered */
	int count = hotbar_visible_slots();

	/* detect hover */
	if (butsel >= BUT_HOTBAR_BEG && butsel <= BUT_HOTBAR_END) {
		hsel = butsel - BUT_HOTBAR_BEG;
	}

	for (int i = 0; i < count; i++) {
		int x = butx(BUT_HOTBAR_BEG + i);
		int y = buty(BUT_HOTBAR_BEG + i);

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

		/* key label — show bound key in corner */
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

	/* spellbook toggle button on the right side of the hotbar */
	{
		int rx = butx(BUT_HOTBAR_BEG + count - 1) + FDX + 8;
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
	if (slot < 0 || slot >= hotbar_visible_slots()) {
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

	return 0;
}

/* spellbook toggle button hit test — called from gui_input on left-click */
int hotbar_toggle_hit(int mx, int my)
{
	int count = hotbar_visible_slots();
	int rx = butx(BUT_HOTBAR_BEG + count - 1) + FDX + 8;
	int ry = buty(BUT_HOTBAR_BEG);
	int dx = mx - rx;
	int dy = my - ry;
	if (dx >= -18 && dx <= 18 && dy >= -18 && dy <= 18) {
		spellbook_toggle();
		return 1;
	}
	return 0;
}
