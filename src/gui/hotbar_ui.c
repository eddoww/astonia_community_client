/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Hotbar UI — renders the 10-slot hotbar and handles drag-and-drop from
 * inventory (items) and spell book (spells) onto hotbar slots.
 */

#include <stdio.h>
#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "client/client.h"
#include "game/game.h"

/* ── Hotbar rendering ──────────────────────────────────────────────────── */

void hotbar_display(void)
{
	RenderFX fx;
	int hsel = -1; /* which hotbar slot is hovered */

	/* detect hover */
	if (butsel >= BUT_HOTBAR_BEG && butsel <= BUT_HOTBAR_END) {
		hsel = butsel - BUT_HOTBAR_BEG;
	}

	for (int i = 0; i < HOTBAR_SLOTS; i++) {
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
		InputBinding *b = input_binding_at(0); /* find by iterating */
		for (int bi = 0; bi < input_binding_count(); bi++) {
			b = input_binding_at(bi);
			if (b && b->category == INPUT_CAT_HOTBAR && b->param == i) {
				break;
			}
			b = NULL;
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
}

/* ── Hotbar click handling ─────────────────────────────────────────────── */

int hotbar_click(int slot)
{
	if (slot < 0 || slot >= HOTBAR_SLOTS) {
		return 0;
	}

	/* if player is carrying an item on cursor, assign it to this slot */
	if (csprite) {
		/* find which inventory slot this csprite came from */
		int found = 0;
		for (int i = 30; i < _inventorysize; i++) {
			if (item[i] == csprite) {
				hotbar_assign_item(slot, i);
				found = 1;
				break;
			}
		}
		if (!found) {
			/* item not found in inventory (maybe in hand only) — assign type without index */
			hotbar_assign_item(slot, 0);
		}
		save_options();
		return 1;
	}

	/* right-click to clear */
	if (vk_rbut) {
		hotbar_clear(slot);
		save_options();
		return 1;
	}

	return 0;
}
