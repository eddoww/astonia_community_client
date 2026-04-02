/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Unified Input Binding System — Implementation
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/input_bind.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "client/client.h"
#include "game/game.h"
#include "modder/modder.h"
#include "gui/escape_menu_ui.h"
#include "gui/keybind_settings_ui.h"
#include "gui/options_ui.h"
#include "lib/cjson/cJSON.h"

/* ── Registry ──────────────────────────────────────────────────────────── */

static InputBinding bindings[INPUT_MAX_BINDINGS];
static int binding_count = 0;
static int version_mask = INPUT_VALL;

static const char *category_names[INPUT_CAT_COUNT] = {
    [INPUT_CAT_SYSTEM] = "System",
    [INPUT_CAT_UI] = "Interface",
    [INPUT_CAT_COMBAT] = "Combat",
    [INPUT_CAT_SPELLS] = "Spells",
    [INPUT_CAT_MOVEMENT] = "Movement",
    [INPUT_CAT_HOTBAR] = "Hotbar",
};

DLL_EXPORT const char *input_category_name(InputCategory cat)
{
	if (cat >= 0 && cat < INPUT_CAT_COUNT) {
		return category_names[cat];
	}
	return "Unknown";
}

/* ── Registration ──────────────────────────────────────────────────────── */

static InputBinding *reg(const char *id, const char *name, InputCategory cat, SDL_Keycode key, Uint8 mods,
    void (*on_press)(InputBinding *self))
{
	if (binding_count >= INPUT_MAX_BINDINGS) {
		return NULL;
	}
	InputBinding *b = &bindings[binding_count++];
	memset(b, 0, sizeof(*b));

	b->id = id;
	b->display_name = name;
	b->category = cat;
	b->key = b->default_key = key;
	b->modifiers = b->default_modifiers = mods;
	b->on_press = on_press;
	b->action_slot = -1;
	b->required_skill = -1;
	b->version_mask = INPUT_VALL;
	b->rebindable = 1;

	return b;
}

DLL_EXPORT InputBinding *input_register(const char *id, const char *display_name, InputCategory cat, SDL_Keycode key,
    Uint8 modifiers, void (*on_press)(InputBinding *self))
{
	return reg(id, display_name, cat, key, modifiers, on_press);
}

/* ── Callback implementations ──────────────────────────────────────────
 *
 * Each callback is a small, self-contained function. The binding's .param
 * and .action_slot fields carry any data the callback needs.
 */

/* system */

static void on_escape(InputBinding *self)
{
	(void)self;
	cmd_stop();
	context_stop();
	show_look = 0;
	display_gfx = 0;
	teleporter = 0;
	show_tutor = 0;
	display_help = 0;
	display_quest = 0;
	show_color = 0;
	context_key_reset();
	action_ovr = ACTION_NONE;
	minimap_hide();
	if (context_key_enabled()) {
		cmd_reset();
	}
	context_key_set(0);
}

static void on_menu(InputBinding *self)
{
	(void)self;
	if (keybind_settings_is_open()) {
		keybind_settings_close();
	} else if (options_is_open()) {
		options_close();
	} else {
		escape_menu_toggle();
	}
}

static void on_quit(InputBinding *self)
{
	(void)self;
	quit = 1;
}

/* UI toggles */

extern int nocut;
extern int display_vc;
extern int display_help;
extern int display_quest;

static void on_toggle_nocut(InputBinding *self)
{
	(void)self;
	nocut ^= 1;
}

static void on_toggle_quest(InputBinding *self)
{
	(void)self;
	if (display_quest) {
		display_quest = 0;
	} else {
		display_help = 0;
		display_quest = 1;
	}
}

static void on_toggle_debug(InputBinding *self)
{
	(void)self;
	display_vc ^= 1;
	list_mem();
	render_list_text();
}

static void on_toggle_help(InputBinding *self)
{
	(void)self;
	if (display_help) {
		display_help = 0;
	} else {
		display_quest = 0;
		display_help = 1;
	}
}

static void on_toggle_minimap(InputBinding *self)
{
	(void)self;
	minimap_toggle();
}

/* hotbar display settings — defined here so toggle callbacks can access them */
static int show_hotkeys = 1;
static int show_names;

static void on_toggle_hotbar_names(InputBinding *self)
{
	(void)self;
	show_names ^= 1;
	init_dots();
	save_options();
}

static void on_toggle_hotbar_keys(InputBinding *self)
{
	(void)self;
	show_hotkeys ^= 1;
	save_options();
}

static void on_chat_pageup(InputBinding *self)
{
	(void)self;
	render_text_pageup();
}

static void on_chat_pagedown(InputBinding *self)
{
	(void)self;
	render_text_pagedown();
}

/* movement */

/* ── Keyboard movement (CL_WALK_DIR) ──────────────────────────────────
 *
 * Uses the server's CL_WALK_DIR command: "walk continuously in this
 * direction until told to stop."  The server handles each step with
 * do_walk() — no A* pathfinding, no idle gaps between steps.
 *
 * The client only sends a packet when the direction changes or
 * movement starts/stops.  All held keys are sampled each game tick
 * to prevent wrong-direction sends when two keys are pressed in
 * quick succession (e.g. W then D for NE).
 *
 * Direction mapping (DX_ values match server direction.h 1-8):
 *   W   → DX_LEFTUP(6)     screen N     W+D → DX_UP(7)       screen NE
 *   S   → DX_RIGHTDOWN(2)  screen S     W+A → DX_LEFT(5)     screen NW
 *   A   → DX_LEFTDOWN(4)   screen W     S+D → DX_RIGHT(1)    screen SE
 *   D   → DX_RIGHTUP(8)    screen E     S+A → DX_DOWN(3)     screen SW
 */

static int kmove_held[4]; /* KMOVE_UP / DOWN / LEFT / RIGHT */
static int kmove_active;
static int kmove_last_dir; /* last direction sent to server (1-8, or 0) */

/* convert world delta to server direction (DX_ constants 1-8) */
static int kmove_delta_to_dir(int dx, int dy)
{
	if (dx == 1 && dy == 0) {
		return 1; /* DX_RIGHT */
	}
	if (dx == 1 && dy == 1) {
		return 2; /* DX_RIGHTDOWN */
	}
	if (dx == 0 && dy == 1) {
		return 3; /* DX_DOWN */
	}
	if (dx == -1 && dy == 1) {
		return 4; /* DX_LEFTDOWN */
	}
	if (dx == -1 && dy == 0) {
		return 5; /* DX_LEFT */
	}
	if (dx == -1 && dy == -1) {
		return 6; /* DX_LEFTUP */
	}
	if (dx == 0 && dy == -1) {
		return 7; /* DX_UP */
	}
	if (dx == 1 && dy == -1) {
		return 8; /* DX_RIGHTUP */
	}
	return 0;
}

static void kmove_compute_delta(int *dx, int *dy)
{
	*dx = 0;
	*dy = 0;

	if (kmove_held[KMOVE_UP]) {
		*dx -= 1;
		*dy -= 1;
	}
	if (kmove_held[KMOVE_DOWN]) {
		*dx += 1;
		*dy += 1;
	}
	if (kmove_held[KMOVE_LEFT]) {
		*dx -= 1;
		*dy += 1;
	}
	if (kmove_held[KMOVE_RIGHT]) {
		*dx += 1;
		*dy -= 1;
	}

	if (*dx > 1) {
		*dx = 1;
	}
	if (*dx < -1) {
		*dx = -1;
	}
	if (*dy > 1) {
		*dy = 1;
	}
	if (*dy < -1) {
		*dy = -1;
	}
}

void keyboard_move_press(int dir)
{
	if (dir < 0 || dir > 3) {
		return;
	}
	kmove_held[dir] = 1;
	kmove_active = 1;
}

void keyboard_move_release(int dir)
{
	if (dir < 0 || dir > 3) {
		return;
	}
	kmove_held[dir] = 0;

	int dx, dy;
	kmove_compute_delta(&dx, &dy);

	if (!dx && !dy) {
		if (kmove_active) {
			cmd_walk_dir(0);
			kmove_active = 0;
			kmove_last_dir = 0;
		}
	}
}

void keyboard_move_tick(void)
{
	int dx, dy;
	kmove_compute_delta(&dx, &dy);

	int dir = (dx || dy) ? kmove_delta_to_dir(dx, dy) : 0;

	if (dir != kmove_last_dir) {
		if (dir) {
			cmd_walk_dir(dir);
		} else {
			cmd_walk_dir(0);
			kmove_active = 0;
		}
		kmove_last_dir = dir;
	}
}

DLL_EXPORT int keyboard_move_active(void)
{
	return kmove_active;
}

static void on_move_dir(InputBinding *self)
{
	keyboard_move_press(self->param);
}

static void on_set_speed(InputBinding *self)
{
	cmd_speed(self->param);
}

/* forward declarations */
static int find_item_in_inventory(uint32_t item_type);
static int resolve_target_override(int action_slot, HotbarTargetOverride tgt);

/* default key assignments for hotbar row 1 — shared between
 * register_all (binding registration) and hotbar_setup_defaults */
static const SDL_Keycode row1_keys[HOTBAR_SLOTS_PER_ROW] = {
    SDLK_1,
    SDLK_2,
    SDLK_3,
    SDLK_4,
    SDLK_5,
    SDLK_6,
    SDLK_7,
    SDLK_8,
    SDLK_9,
    SDLK_0,
    'q',
    'e',
    'z',
    'x',
    'r',
};

/* ── Hotbar ─────────────────────────────────────────────────────────────
 *
 * Each slot holds either an item or a spell. Item slots auto-refill from
 * inventory when consumed. Spell slots activate the action bar system.
 */

static HotbarSlot hotbar[HOTBAR_MAX_SLOTS];
static int visible_slots = HOTBAR_DEFAULT_SLOTS;
static int active_rows = 1; /* how many hotbar rows are visible (1-3) */
static int cast_mode = CAST_NORMAL; /* how targeted spells are cast from hotkeys */

DLL_EXPORT int hotbar_visible_slots(void)
{
	return visible_slots;
}

DLL_EXPORT int hotbar_rows(void)
{
	return active_rows;
}

DLL_EXPORT void hotbar_set_rows(int count)
{
	if (count < 1) {
		count = 1;
	}
	if (count > HOTBAR_MAX_ROWS) {
		count = HOTBAR_MAX_ROWS;
	}
	active_rows = count;
}

DLL_EXPORT void hotbar_set_visible_slots(int count)
{
	if (count < 1) {
		count = 1;
	}
	if (count > HOTBAR_MAX_SLOTS) {
		count = HOTBAR_MAX_SLOTS;
	}
	visible_slots = count;
}

DLL_EXPORT int hotbar_cast_mode(void)
{
	return cast_mode;
}

DLL_EXPORT void hotbar_set_cast_mode(int mode)
{
	if (mode >= CAST_NORMAL && mode <= CAST_QUICK_INDICATOR) {
		cast_mode = mode;
	}
}

DLL_EXPORT int hotbar_show_hotkeys(void)
{
	return show_hotkeys;
}

DLL_EXPORT void hotbar_set_show_hotkeys(int on)
{
	show_hotkeys = on ? 1 : 0;
}

DLL_EXPORT int hotbar_show_names(void)
{
	return show_names;
}

DLL_EXPORT void hotbar_set_show_names(int on)
{
	show_names = on ? 1 : 0;
}

DLL_EXPORT void hotbar_assign_item(int slot, int inventory_index)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}
	memset(&hotbar[slot], 0, sizeof(hotbar[slot]));
	hotbar[slot].type = HOTBAR_ITEM;
	hotbar[slot].inv_index = inventory_index;
	hotbar[slot].item_type = (inventory_index > 0 && inventory_index < _inventorysize) ? item[inventory_index] : 0;
}

DLL_EXPORT void hotbar_assign_item_by_type(int slot, uint32_t item_type)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}
	memset(&hotbar[slot], 0, sizeof(hotbar[slot]));
	hotbar[slot].type = HOTBAR_ITEM;
	hotbar[slot].item_type = item_type;
	hotbar[slot].inv_index = find_item_in_inventory(item_type);
}

DLL_EXPORT void hotbar_assign_spell(int slot, int action_slot)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}
	memset(&hotbar[slot], 0, sizeof(hotbar[slot]));
	hotbar[slot].type = HOTBAR_SPELL;
	hotbar[slot].action_slot = action_slot;
}

DLL_EXPORT void hotbar_clear(int slot)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}
	memset(&hotbar[slot], 0, sizeof(hotbar[slot]));
}

DLL_EXPORT void hotbar_clear_all(void)
{
	memset(hotbar, 0, sizeof(hotbar));
}

DLL_EXPORT void hotbar_swap(int a, int b)
{
	if (a < 0 || a >= HOTBAR_MAX_SLOTS || b < 0 || b >= HOTBAR_MAX_SLOTS || a == b) {
		return;
	}
	HotbarSlot tmp = hotbar[a];
	hotbar[a] = hotbar[b];
	hotbar[b] = tmp;
}

/* set up default hotbar contents for first-time users (no saved config).
 *
 * Default modifier scheme:
 *   no modifier  = normal cast (primary target)
 *   shift + key  = quick cast (same target as primary)
 *   ctrl + key   = indicator cast, character target
 */
void hotbar_setup_defaults(void)
{
	static const struct {
		int action_slot;
		HotbarTargetOverride primary_target; /* DEFAULT = use resolve_spell_action */
	} spell_defaults[] = {
	    {ACTION_ATTACK, HOTBAR_TGT_DEFAULT}, /* 1 — chr only */
	    {ACTION_FIREBALL, HOTBAR_TGT_MAP}, /* 2 — default to map cast */
	    {ACTION_LBALL, HOTBAR_TGT_MAP}, /* 3 — default to map cast */
	    {ACTION_FLASH, HOTBAR_TGT_DEFAULT}, /* 4 — self-only */
	    {ACTION_FREEZE, HOTBAR_TGT_DEFAULT}, /* 5 — self-only */
	    {ACTION_BLESS, HOTBAR_TGT_SELF}, /* 6 — default to self */
	    {ACTION_SHIELD, HOTBAR_TGT_DEFAULT}, /* 7 — self-only */
	    {ACTION_HEAL, HOTBAR_TGT_SELF}, /* 8 — default to self */
	    {ACTION_WARCRY, HOTBAR_TGT_DEFAULT}, /* 9 — self-only */
	    {ACTION_PULSE, HOTBAR_TGT_DEFAULT}, /* 0 — self-only */
	    {ACTION_FIRERING, HOTBAR_TGT_DEFAULT}, /* Q — self-only */
	    {-1, HOTBAR_TGT_DEFAULT}, /* W — empty (potions) */
	    {ACTION_TAKEGIVE, HOTBAR_TGT_DEFAULT}, /* E */
	    {ACTION_MAP, HOTBAR_TGT_DEFAULT}, /* R */
	    {ACTION_LOOK, HOTBAR_TGT_DEFAULT}, /* T */
	};

	int num_defaults = (int)(sizeof(spell_defaults) / sizeof(spell_defaults[0]));
	for (int i = 0; i < num_defaults; i++) {
		if (spell_defaults[i].action_slot < 0) {
			continue;
		}
		int action = spell_defaults[i].action_slot;
		hotbar_assign_spell(i, action);
		hotbar[i].primary_target = spell_defaults[i].primary_target;

		if (i >= HOTBAR_SLOTS_PER_ROW) {
			continue;
		}

		int vtgt = hotbar_spell_valid_targets(action);
		int has_cast = hotbar_spell_has_cast_modes(action);
		if (!has_cast) {
			continue; /* self-only spells: no extra binds needed */
		}

		/* shift + key = quick cast (same target as primary) */
		hotbar_add_bind(i, row1_keys[i], INPUT_MOD_SHIFT, HOTBAR_CAST_QUICK, HOTBAR_TGT_DEFAULT);

		/* ctrl + key = indicator cast, character target (only if chr is valid) */
		if (vtgt & HOTBAR_VTGT_CHR) {
			hotbar_add_bind(i, row1_keys[i], INPUT_MOD_CTRL, HOTBAR_CAST_INDICATOR, HOTBAR_TGT_CHR);
		}
	}
}

DLL_EXPORT const HotbarSlot *hotbar_get(int slot)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return NULL;
	}
	return &hotbar[slot];
}

void hotbar_set_primary_target(int slot, HotbarTargetOverride tgt)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}
	hotbar[slot].primary_target = tgt;
}

DLL_EXPORT uint32_t hotbar_slot_sprite(int slot)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return 0;
	}

	switch (hotbar[slot].type) {
	case HOTBAR_ITEM:
		if (hotbar[slot].inv_index > 0) {
			return item[hotbar[slot].inv_index];
		}
		return hotbar[slot].item_type; /* show greyed-out icon if out of stock */
	case HOTBAR_SPELL:
		return (uint32_t)(SPELL_ICON_SPRITE_BASE + hotbar[slot].action_slot);
	default:
		return 0;
	}
}

DLL_EXPORT const char *hotbar_slot_name(int slot)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return NULL;
	}
	switch (hotbar[slot].type) {
	case HOTBAR_SPELL:
		return get_action_text(hotbar[slot].action_slot);
	case HOTBAR_ITEM:
		if (hotbar[slot].inv_index > 0) {
			return hover_get_item_name(hotbar[slot].inv_index);
		}
		return NULL;
	default:
		return NULL;
	}
}

/* find the next inventory slot holding the same item type (skip equipment) */
static int find_item_in_inventory(uint32_t item_type)
{
	if (item_type == 0) {
		return 0;
	}
	for (int i = INVENTORY_EQUIP_SLOTS; i < _inventorysize; i++) {
		if (item[i] == item_type && (item_flags[i] & IF_USE)) {
			return i;
		}
	}
	return 0;
}

void hotbar_on_item_changed(int inventory_index)
{
	if (inventory_index < 0 || inventory_index >= _inventorysize) {
		return;
	}
	for (int s = 0; s < HOTBAR_MAX_SLOTS; s++) {
		if (hotbar[s].type != HOTBAR_ITEM || hotbar[s].item_type == 0) {
			continue;
		}

		/* slot has no inventory index yet (e.g. after loading config) —
		 * try to resolve it now that an inventory slot was updated */
		if (hotbar[s].inv_index <= 0) {
			hotbar[s].inv_index = find_item_in_inventory(hotbar[s].item_type);
			continue;
		}

		/* only care about the slot that actually changed */
		if (hotbar[s].inv_index != inventory_index) {
			continue;
		}

		/* still the same item type? nothing to do */
		if (item[inventory_index] == hotbar[s].item_type) {
			continue;
		}

		/* item was consumed/removed — find the next one of the same type */
		hotbar[s].inv_index = find_item_in_inventory(hotbar[s].item_type);
	}
}

/* ── Per-spell capability table ─────────────────────────────────────── */

#define ACTION_COUNT 14

static const struct {
	int valid_targets;
	int has_cast_modes;
} spell_caps[ACTION_COUNT] = {
    [ACTION_ATTACK] = {HOTBAR_VTGT_CHR, 1},
    [ACTION_FIREBALL] = {HOTBAR_VTGT_CHR | HOTBAR_VTGT_MAP, 1},
    [ACTION_LBALL] = {HOTBAR_VTGT_CHR | HOTBAR_VTGT_MAP, 1},
    [ACTION_FLASH] = {HOTBAR_VTGT_SELF, 0},
    [ACTION_FREEZE] = {HOTBAR_VTGT_SELF, 0},
    [ACTION_SHIELD] = {HOTBAR_VTGT_SELF, 0},
    [ACTION_BLESS] = {HOTBAR_VTGT_CHR | HOTBAR_VTGT_SELF, 1},
    [ACTION_HEAL] = {HOTBAR_VTGT_CHR | HOTBAR_VTGT_SELF, 1},
    [ACTION_WARCRY] = {HOTBAR_VTGT_SELF, 0},
    [ACTION_PULSE] = {HOTBAR_VTGT_SELF, 0},
    [ACTION_FIRERING] = {HOTBAR_VTGT_SELF, 0},
    [ACTION_TAKEGIVE] = {HOTBAR_VTGT_CHR | HOTBAR_VTGT_MAP, 1},
    [ACTION_MAP] = {0, 0},
    [ACTION_LOOK] = {HOTBAR_VTGT_CHR | HOTBAR_VTGT_MAP, 1},
};

DLL_EXPORT int hotbar_spell_valid_targets(int action_slot)
{
	if (action_slot < 0 || action_slot >= ACTION_COUNT) {
		return 0;
	}
	return spell_caps[action_slot].valid_targets;
}

DLL_EXPORT int hotbar_spell_has_cast_modes(int action_slot)
{
	if (action_slot < 0 || action_slot >= ACTION_COUNT) {
		return 0;
	}
	return spell_caps[action_slot].has_cast_modes;
}

/*
 * Resolve which context action_slot to use for a spell.
 *
 * The old action bar had two rows:
 *   Row 0 (slot 0-13):    target character / interact
 *   Row 1 (slot 100-113): self-cast or map-cast
 *
 * Spells fall into three categories:
 *   Self-only  (Flash, Freeze, Shield, Warcry, Pulse, Firering)
 *     → always 100+slot, no modifier needed
 *   Dual-mode  (Fireball, LBall: char vs map; Bless, Heal: char vs self)
 *     → default = slot (target character), Shift = 100+slot (self/map)
 *   Action-only (Attack, TakeGive, Look)
 *     → always slot, no alternative
 */
static int resolve_spell_action(int action_slot)
{
	int shift = vk_shift || vk_item; /* shift held */

	switch (action_slot) {
	/* self-only spells and toggles — always use the 100+ variant */
	case ACTION_FLASH:
	case ACTION_FREEZE:
	case ACTION_SHIELD:
	case ACTION_WARCRY:
	case ACTION_PULSE:
	case ACTION_FIRERING:
	case ACTION_MAP:
		return 100 + action_slot;

	/* dual-mode spells — shift toggles to alternative */
	case ACTION_FIREBALL:
	case ACTION_LBALL:
	case ACTION_BLESS:
	case ACTION_HEAL:
		return shift ? (100 + action_slot) : action_slot;

	/* everything else (attack, take/give, look, map) — no alternative */
	default:
		return action_slot;
	}
}

/* mark a slot as activated for visual feedback */
static void hotbar_flash(int slot)
{
	if (slot >= 0 && slot < HOTBAR_MAX_SLOTS) {
		hotbar[slot].activated_at = tick;
	}
}

/* track which slot is being held for Quick Cast w/ Indicator */
static int held_hotbar_slot = -1;
static int held_action_resolved = -1;

/* try to use an item from a hotbar slot. returns 1 if used. */
static int hotbar_try_use_item(int slot)
{
	int inv_idx = hotbar[slot].inv_index;
	if (inv_idx > 0 && item[inv_idx]) {
		hotbar_flash(slot);
		cmd_use_inv(inv_idx);
		return 1;
	}
	return 0;
}

/* cast a spell from a hotbar slot using the given cast mode and resolved action. */
static void hotbar_cast_spell(int slot, int resolved, int mode)
{
	switch (mode) {
	case CAST_QUICK:
		hotbar_flash(slot);
		context_execute_action(resolved);
		break;
	case CAST_QUICK_INDICATOR:
		/* hold key → show targeting indicator → release key → cast.
		 * don't flash on press (flash happens on release when the spell fires).
		 * ignore key repeats if already holding this slot. */
		if (held_hotbar_slot == slot) {
			return;
		}
		context_activate_action(resolved);
		held_hotbar_slot = slot;
		held_action_resolved = resolved;
		break;
	case CAST_NORMAL:
	default:
		/* normal: enter targeting mode, click to confirm */
		hotbar_flash(slot);
		context_execute_action_normal(resolved);
		break;
	}
}

/* activate a hotbar slot with a given cast mode.
 * clicks pass CAST_NORMAL; hotkey presses pass the global cast_mode. */
DLL_EXPORT void hotbar_activate_with_mode(int slot, int mode)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}

	if (amod_hotbar_activate(slot, mode)) {
		return;
	}

	switch (hotbar[slot].type) {
	case HOTBAR_ITEM:
		hotbar_try_use_item(slot);
		break;
	case HOTBAR_SPELL: {
		int resolved;
		if (hotbar[slot].primary_target != HOTBAR_TGT_DEFAULT) {
			resolved = resolve_target_override(hotbar[slot].action_slot, hotbar[slot].primary_target);
		} else {
			resolved = resolve_spell_action(hotbar[slot].action_slot);
		}
		if (mode != CAST_QUICK && (input_current_modifiers() & INPUT_MOD_SHIFT)) {
			mode = CAST_QUICK;
		}
		hotbar_cast_spell(slot, resolved, mode);
		break;
	}
	default:
		break;
	}
}

/* activate a hotbar slot from a mouse click — always uses normal cast */
DLL_EXPORT void hotbar_activate(int slot)
{
	hotbar_activate_with_mode(slot, CAST_NORMAL);
}

/* called on hotkey release — fires held spell for Quick Cast w/ Indicator mode.
 * only checks held state (not global cast_mode) so per-bind overrides work. */
void hotbar_hotkey_release(int slot)
{
	if (held_hotbar_slot != slot || held_action_resolved < 0) {
		return;
	}

	/* execute the held spell at the current cursor position */
	hotbar_flash(held_hotbar_slot);
	context_execute_action(held_action_resolved);
	context_key_reset();
	held_hotbar_slot = -1;
	held_action_resolved = -1;
}

/* cancel a Quick Cast w/ Indicator hold (e.g. on right-click) */
void hotbar_cancel_held(void)
{
	if (held_hotbar_slot >= 0) {
		context_key_reset();
		held_hotbar_slot = -1;
		held_action_resolved = -1;
	}
}

/* ── Multi-bind: extra keybindings per hotbar slot ────────────────────── */

int hotbar_add_bind(int slot, SDL_Keycode key, Uint8 mods, HotbarCastOverride cast, HotbarTargetOverride target)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return -1;
	}
	if (hotbar[slot].extra_bind_count >= HOTBAR_MAX_BINDS) {
		return -1;
	}

	int idx = hotbar[slot].extra_bind_count++;
	hotbar[slot].extra_binds[idx].key = key;
	hotbar[slot].extra_binds[idx].modifiers = mods;
	hotbar[slot].extra_binds[idx].cast_override = cast;
	hotbar[slot].extra_binds[idx].target_override = target;
	return idx;
}

int hotbar_remove_bind(int slot, int bind_index)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return -1;
	}
	if (bind_index < 0 || bind_index >= hotbar[slot].extra_bind_count) {
		return -1;
	}

	/* shift remaining entries down */
	for (int i = bind_index; i < hotbar[slot].extra_bind_count - 1; i++) {
		hotbar[slot].extra_binds[i] = hotbar[slot].extra_binds[i + 1];
	}
	hotbar[slot].extra_bind_count--;
	memset(&hotbar[slot].extra_binds[hotbar[slot].extra_bind_count], 0, sizeof(HotbarBind));
	return 0;
}

void hotbar_clear_binds(int slot)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}
	memset(hotbar[slot].extra_binds, 0, sizeof(hotbar[slot].extra_binds));
	hotbar[slot].extra_bind_count = 0;
}

int hotbar_set_bind_key(int slot, int bind_index, SDL_Keycode key, Uint8 mods)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return -1;
	}
	if (bind_index < 0 || bind_index >= hotbar[slot].extra_bind_count) {
		return -1;
	}
	hotbar[slot].extra_binds[bind_index].key = key;
	hotbar[slot].extra_binds[bind_index].modifiers = mods;
	return 0;
}

int hotbar_set_bind_cast(int slot, int bind_index, HotbarCastOverride cast)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return -1;
	}
	if (bind_index < 0 || bind_index >= hotbar[slot].extra_bind_count) {
		return -1;
	}
	hotbar[slot].extra_binds[bind_index].cast_override = cast;
	return 0;
}

int hotbar_set_bind_target(int slot, int bind_index, HotbarTargetOverride target)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return -1;
	}
	if (bind_index < 0 || bind_index >= hotbar[slot].extra_bind_count) {
		return -1;
	}
	hotbar[slot].extra_binds[bind_index].target_override = target;
	return 0;
}

int hotbar_find_extra_bind(SDL_Keycode key, Uint8 mods)
{
	int count = hotbar_visible_slots() * hotbar_rows();
	for (int s = 0; s < count; s++) {
		for (int b = 0; b < hotbar[s].extra_bind_count; b++) {
			if (hotbar[s].extra_binds[b].key == key && hotbar[s].extra_binds[b].modifiers == mods) {
				return s;
			}
		}
	}
	return -1;
}

/* resolve the effective action slot from target override */
static int resolve_target_override(int action_slot, HotbarTargetOverride tgt)
{
	switch (tgt) {
	case HOTBAR_TGT_MAP:
	case HOTBAR_TGT_SELF:
		return 100 + action_slot;
	case HOTBAR_TGT_CHR:
		return action_slot;
	default:
		return resolve_spell_action(action_slot);
	}
}

/* resolve the effective cast mode from cast override */
static int resolve_cast_override(HotbarCastOverride co)
{
	switch (co) {
	case HOTBAR_CAST_NORMAL:
		return CAST_NORMAL;
	case HOTBAR_CAST_QUICK:
		return CAST_QUICK;
	case HOTBAR_CAST_INDICATOR:
		return CAST_QUICK_INDICATOR;
	default:
		return cast_mode;
	}
}

void hotbar_activate_extra(int slot, SDL_Keycode key, Uint8 mods)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}

	/* find the matching extra bind */
	const HotbarBind *bind = NULL;
	for (int i = 0; i < hotbar[slot].extra_bind_count; i++) {
		if (hotbar[slot].extra_binds[i].key == key && hotbar[slot].extra_binds[i].modifiers == mods) {
			bind = &hotbar[slot].extra_binds[i];
			break;
		}
	}
	if (!bind) {
		return;
	}

	switch (hotbar[slot].type) {
	case HOTBAR_ITEM:
		hotbar_try_use_item(slot);
		break;
	case HOTBAR_SPELL:
		hotbar_cast_spell(slot, resolve_target_override(hotbar[slot].action_slot, bind->target_override),
		    resolve_cast_override(bind->cast_override));
		break;
	default:
		break;
	}
}

static void on_hotbar_press(InputBinding *self)
{
	hotbar_activate_with_mode(self->param, cast_mode);
}

/* called on any key release — checks if it matches a held hotbar binding */
void input_keyup(SDL_Keycode key)
{
	/* release keyboard movement direction if this key is a movement binding */
	switch (key) {
	case SDLK_UP:
		keyboard_move_release(KMOVE_UP);
		break;
	case SDLK_DOWN:
		keyboard_move_release(KMOVE_DOWN);
		break;
	case SDLK_LEFT:
		keyboard_move_release(KMOVE_LEFT);
		break;
	case SDLK_RIGHT:
		keyboard_move_release(KMOVE_RIGHT);
		break;
	default:
		/* check WASD (rebindable) movement bindings */
		for (int i = 0; i < binding_count; i++) {
			InputBinding *b = &bindings[i];
			if (b->on_press == on_move_dir && b->key == key) {
				keyboard_move_release(b->param);
				break;
			}
		}
		break;
	}

	if (held_hotbar_slot < 0) {
		return;
	}

	/* check primary binding for the held slot */
	for (int i = 0; i < binding_count; i++) {
		InputBinding *b = &bindings[i];
		if (b->category == INPUT_CAT_HOTBAR && b->param == held_hotbar_slot && b->key == key) {
			hotbar_hotkey_release(held_hotbar_slot);
			return;
		}
	}

	/* check extra binds for the held slot */
	for (int i = 0; i < hotbar[held_hotbar_slot].extra_bind_count; i++) {
		if (hotbar[held_hotbar_slot].extra_binds[i].key == key) {
			hotbar_hotkey_release(held_hotbar_slot);
			return;
		}
	}
}

/* ── Default bindings ──────────────────────────────────────────────────── */

static void register_all(void)
{
	InputBinding *b;

	/* ── System (locked) ─────────────────────────────────────────── */

	reg("ui.cancel", "Cancel All", INPUT_CAT_UI, SDLK_UNKNOWN, 0, on_escape);
	reg("ui.menu", "Open Menu", INPUT_CAT_UI, SDLK_ESCAPE, 0, on_menu);

	b = reg("sys.quit", "Quit Game", INPUT_CAT_SYSTEM, SDLK_F12, 0, on_quit);
	if (b) {
		b->rebindable = 0;
	}

	/* ── UI ───────────────────────────────────────────────────────── */

	reg("ui.toggle_nocut", "Toggle Sprite Cutting", INPUT_CAT_UI, SDLK_F8, 0, on_toggle_nocut);
	reg("ui.toggle_quest", "Toggle Quest Log", INPUT_CAT_UI, SDLK_F9, 0, on_toggle_quest);
	reg("ui.toggle_debug", "Toggle Debug Info", INPUT_CAT_UI, SDLK_F10, 0, on_toggle_debug);
	reg("ui.toggle_help", "Toggle Help", INPUT_CAT_UI, SDLK_F11, 0, on_toggle_help);
	reg("ui.toggle_minimap", "Toggle Minimap", INPUT_CAT_UI, 'm', INPUT_MOD_SHIFT | INPUT_MOD_CTRL, on_toggle_minimap);
	reg("ui.toggle_hotbar_names", "Toggle Hotbar Names", INPUT_CAT_UI, SDLK_UNKNOWN, 0, on_toggle_hotbar_names);
	reg("ui.toggle_hotbar_keys", "Toggle Hotbar Key Labels", INPUT_CAT_UI, SDLK_UNKNOWN, 0, on_toggle_hotbar_keys);
	reg("ui.chat_pageup", "Chat Page Up", INPUT_CAT_UI, SDLK_PAGEUP, 0, on_chat_pageup);
	reg("ui.chat_pagedown", "Chat Page Down", INPUT_CAT_UI, SDLK_PAGEDOWN, 0, on_chat_pagedown);

	/* ── Movement ─────────────────────────────────────────────────── */

	b = reg("move.speed_fast", "Fast Speed", INPUT_CAT_MOVEMENT, SDLK_F5, 0, on_set_speed);
	if (b) {
		b->param = 1;
	}
	b = reg("move.speed_normal", "Normal Speed", INPUT_CAT_MOVEMENT, SDLK_F6, 0, on_set_speed);
	if (b) {
		b->param = 0;
	}
	b = reg("move.speed_stealth", "Stealth Speed", INPUT_CAT_MOVEMENT, SDLK_F7, 0, on_set_speed);
	if (b) {
		b->param = 2;
	}

	b = reg("move.up", "Move Up", INPUT_CAT_MOVEMENT, 'w', 0, on_move_dir);
	if (b) {
		b->param = KMOVE_UP;
	}
	b = reg("move.down", "Move Down", INPUT_CAT_MOVEMENT, 's', 0, on_move_dir);
	if (b) {
		b->param = KMOVE_DOWN;
	}
	b = reg("move.left", "Move Left", INPUT_CAT_MOVEMENT, 'a', 0, on_move_dir);
	if (b) {
		b->param = KMOVE_LEFT;
	}
	b = reg("move.right", "Move Right", INPUT_CAT_MOVEMENT, 'd', 0, on_move_dir);
	if (b) {
		b->param = KMOVE_RIGHT;
	}

	/* ── Hotbar (3 rows × 15 slots) ──────────────────────────────── */

	/* generate hotbar binding entries: row 1 has keys, rows 2-3 unbound */
	static struct {
		char id[16];
		char name[24];
		SDL_Keycode key;
	} hotbar_defaults[HOTBAR_MAX_SLOTS];

	for (int i = 0; i < HOTBAR_MAX_SLOTS; i++) {
		snprintf(hotbar_defaults[i].id, sizeof(hotbar_defaults[i].id), "hotbar.%d", i);
		snprintf(hotbar_defaults[i].name, sizeof(hotbar_defaults[i].name), "Hotbar Slot %d", i + 1);
		hotbar_defaults[i].key = (i < HOTBAR_SLOTS_PER_ROW) ? row1_keys[i] : SDLK_UNKNOWN;
	}

	for (int i = 0; i < HOTBAR_MAX_SLOTS; i++) {
		b = reg(hotbar_defaults[i].id, hotbar_defaults[i].name, INPUT_CAT_HOTBAR, hotbar_defaults[i].key, 0,
		    on_hotbar_press);
		if (b) {
			b->param = i;
		}
	}
}

/* ── Init / Shutdown ───────────────────────────────────────────────────── */

void input_init(int sv_ver)
{
	memset(bindings, 0, sizeof(bindings));
	binding_count = 0;
	version_mask = (sv_ver == 35) ? INPUT_V35 : INPUT_V3;

	register_all();
}

static const char *undo_id;
static SDL_Keycode undo_key;
static Uint8 undo_mods;
static const char *undo_conflict_id;
static SDL_Keycode undo_conflict_key;
static Uint8 undo_conflict_mods;

void input_shutdown(void)
{
	binding_count = 0;
	version_mask = INPUT_VALL;

	/* hotbar */
	memset(hotbar, 0, sizeof(hotbar));
	visible_slots = HOTBAR_DEFAULT_SLOTS;
	active_rows = 1;
	cast_mode = CAST_NORMAL;
	show_hotkeys = 1;
	show_names = 0;

	/* Quick Cast w/ Indicator hold state */
	held_hotbar_slot = -1;
	held_action_resolved = -1;

	/* keyboard movement */
	memset(kmove_held, 0, sizeof(kmove_held));
	kmove_active = 0;
	kmove_last_dir = 0;

	undo_id = NULL;
}

/* ── Modifier helper ───────────────────────────────────────────────────── */

DLL_EXPORT Uint8 input_current_modifiers(void)
{
	SDL_Keymod km = SDL_GetModState();
	Uint8 m = INPUT_MOD_NONE;
	if (km & SDL_KMOD_SHIFT) {
		m |= INPUT_MOD_SHIFT;
	}
	if (km & SDL_KMOD_CTRL) {
		m |= INPUT_MOD_CTRL;
	}
	if (km & SDL_KMOD_ALT) {
		m |= INPUT_MOD_ALT;
	}
	return m;
}

/* ── Numpad normalization ──────────────────────────────────────────────── */

static SDL_Keycode normalize_numpad(SDL_Keycode key)
{
	if (key >= SDLK_KP_0 && key <= SDLK_KP_9) {
		return (SDL_Keycode)('0' + (key - SDLK_KP_0));
	}
	if (key == SDLK_KP_ENTER) {
		return SDLK_RETURN;
	}
	return key;
}

/* ── Lookup ────────────────────────────────────────────────────────────── */

DLL_EXPORT InputBinding *input_find(SDL_Keycode key, Uint8 modifiers)
{
	key = normalize_numpad(key);

	for (int i = 0; i < binding_count; i++) {
		InputBinding *b = &bindings[i];
		if (b->key != key) {
			continue;
		}
		if (b->modifiers != modifiers) {
			continue;
		}
		if (!(b->version_mask & version_mask)) {
			continue;
		}
		return b;
	}
	return NULL;
}

DLL_EXPORT InputBinding *input_find_by_id(const char *id)
{
	if (!id) {
		return NULL;
	}
	for (int i = 0; i < binding_count; i++) {
		if (strcmp(bindings[i].id, id) == 0) {
			return &bindings[i];
		}
	}
	return NULL;
}

/* ── Execution ─────────────────────────────────────────────────────────── */

DLL_EXPORT void input_execute(InputBinding *b)
{
	if (b && b->on_press) {
		b->last_used = now;
		b->on_press(b);
	}
}

/* ── Queries ───────────────────────────────────────────────────────────── */

DLL_EXPORT SDL_Keycode input_get_key(const char *id)
{
	InputBinding *b = input_find_by_id(id);
	return b ? b->key : SDLK_UNKNOWN;
}

DLL_EXPORT int input_binding_count(void)
{
	return binding_count;
}

DLL_EXPORT InputBinding *input_binding_at(int index)
{
	if (index < 0 || index >= binding_count) {
		return NULL;
	}
	return &bindings[index];
}

/* ── Conflict detection ────────────────────────────────────────────────── */

InputBinding *input_find_conflict(SDL_Keycode key, Uint8 modifiers, const char *exclude_id)
{
	if (key == SDLK_UNKNOWN) {
		return NULL;
	}

	for (int i = 0; i < binding_count; i++) {
		InputBinding *b = &bindings[i];
		if (exclude_id && strcmp(b->id, exclude_id) == 0) {
			continue;
		}
		if (b->key != key || b->modifiers != modifiers) {
			continue;
		}
		if (!(b->version_mask & version_mask)) {
			continue;
		}
		return b;
	}
	return NULL;
}

/* ── Rebinding ─────────────────────────────────────────────────────────── */


DLL_EXPORT int input_rebind(const char *id, SDL_Keycode key, Uint8 modifiers)
{
	InputBinding *b = input_find_by_id(id);
	if (!b) {
		return -1;
	}
	if (!b->rebindable) {
		return -2;
	}

	undo_id = b->id;
	undo_key = b->key;
	undo_mods = b->modifiers;
	undo_conflict_id = NULL;

	InputBinding *conflict = input_find_conflict(key, modifiers, id);
	if (conflict && conflict->rebindable) {
		addline("Keybind conflict: '%s' unbound (was %s)", conflict->display_name,
		    input_key_to_string(conflict->key, conflict->modifiers));
		undo_conflict_id = conflict->id;
		undo_conflict_key = conflict->key;
		undo_conflict_mods = conflict->modifiers;
		conflict->key = SDLK_UNKNOWN;
		conflict->modifiers = INPUT_MOD_NONE;
	}

	b->key = key;
	b->modifiers = modifiers;
	return 0;
}

DLL_EXPORT int input_undo_rebind(void)
{
	if (!undo_id) {
		return -1;
	}
	InputBinding *b = input_find_by_id(undo_id);
	if (!b) {
		return -1;
	}
	b->key = undo_key;
	b->modifiers = undo_mods;

	if (undo_conflict_id) {
		InputBinding *c = input_find_by_id(undo_conflict_id);
		if (c) {
			c->key = undo_conflict_key;
			c->modifiers = undo_conflict_mods;
		}
	}

	undo_id = NULL;
	return 0;
}

DLL_EXPORT int input_unbind(const char *id)
{
	InputBinding *b = input_find_by_id(id);
	if (!b) {
		return -1;
	}
	if (!b->rebindable) {
		return -2;
	}
	b->key = SDLK_UNKNOWN;
	b->modifiers = INPUT_MOD_NONE;
	return 0;
}

DLL_EXPORT void input_reset_one(const char *id)
{
	InputBinding *b = input_find_by_id(id);
	if (b) {
		b->key = b->default_key;
		b->modifiers = b->default_modifiers;
	}
}

DLL_EXPORT void input_reset_all(void)
{
	for (int i = 0; i < binding_count; i++) {
		bindings[i].key = bindings[i].default_key;
		bindings[i].modifiers = bindings[i].default_modifiers;
	}
}

DLL_EXPORT void input_load_modern_defaults(void)
{
	input_reset_all();
	input_rebind("ui.menu", SDLK_ESCAPE, INPUT_MOD_NONE);
	input_rebind("ui.cancel", SDLK_UNKNOWN, INPUT_MOD_NONE);
	hotbar_setup_defaults();
}

static void hotbar_setup_legacy_defaults(void)
{
	static const SDL_Keycode row0_keys[] = {
	    'a',
	    's',
	    'd',
	    SDLK_UNKNOWN,
	    SDLK_UNKNOWN,
	    SDLK_UNKNOWN,
	    'f',
	    'g',
	    SDLK_UNKNOWN,
	    SDLK_UNKNOWN,
	    SDLK_UNKNOWN,
	    'h',
	    SDLK_UNKNOWN,
	    'l',
	};
	static const SDL_Keycode row1_legacy[] = {
	    SDLK_UNKNOWN,
	    SDLK_UNKNOWN,
	    SDLK_UNKNOWN,
	    'e',
	    'r',
	    't',
	    'z',
	    'u',
	    'i',
	    'o',
	    'p',
	    SDLK_UNKNOWN,
	    'm',
	    SDLK_UNKNOWN,
	};
	static const HotbarTargetOverride row0_tgt[] = {
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_CHR,
	    HOTBAR_TGT_CHR,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_CHR,
	    HOTBAR_TGT_CHR,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	};
	static const HotbarTargetOverride row1_tgt[] = {
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_SELF,
	    HOTBAR_TGT_SELF,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	    HOTBAR_TGT_DEFAULT,
	};
	int num = 14;

	hotbar_clear_all();
	for (int i = 0; i < binding_count; i++) {
		if (bindings[i].category == INPUT_CAT_HOTBAR) {
			bindings[i].key = SDLK_UNKNOWN;
			bindings[i].modifiers = INPUT_MOD_NONE;
		}
	}

	for (int i = 0; i < num; i++) {
		hotbar_assign_spell(i, i);
		hotbar[i].primary_target = row0_tgt[i];

		char id[32];
		snprintf(id, sizeof(id), "hotbar.%d", i);
		if (row0_keys[i] != SDLK_UNKNOWN) {
			input_rebind(id, row0_keys[i], INPUT_MOD_NONE);
		}

		if (row1_legacy[i] != SDLK_UNKNOWN) {
			hotbar[i].extra_binds[0].key = (uint32_t)row1_legacy[i];
			hotbar[i].extra_binds[0].modifiers = INPUT_MOD_NONE;
			hotbar[i].extra_binds[0].target_override = row1_tgt[i];
			hotbar[i].extra_binds[0].cast_override = HOTBAR_CAST_DEFAULT;
			hotbar[i].extra_bind_count = 1;
		}
	}

	hotbar[ACTION_FIREBALL].extra_binds[0].key = 'q';
	hotbar[ACTION_FIREBALL].extra_binds[0].modifiers = INPUT_MOD_ALT;
	hotbar[ACTION_FIREBALL].extra_binds[0].target_override = HOTBAR_TGT_MAP;
	hotbar[ACTION_FIREBALL].extra_binds[0].cast_override = HOTBAR_CAST_DEFAULT;
	hotbar[ACTION_FIREBALL].extra_binds[1].key = 'q';
	hotbar[ACTION_FIREBALL].extra_binds[1].modifiers = INPUT_MOD_CTRL;
	hotbar[ACTION_FIREBALL].extra_binds[1].target_override = HOTBAR_TGT_CHR;
	hotbar[ACTION_FIREBALL].extra_binds[1].cast_override = HOTBAR_CAST_DEFAULT;
	hotbar[ACTION_FIREBALL].extra_bind_count = 2;

	hotbar[ACTION_LBALL].extra_binds[0].key = 'w';
	hotbar[ACTION_LBALL].extra_binds[0].modifiers = INPUT_MOD_ALT;
	hotbar[ACTION_LBALL].extra_binds[0].target_override = HOTBAR_TGT_MAP;
	hotbar[ACTION_LBALL].extra_binds[0].cast_override = HOTBAR_CAST_DEFAULT;
	hotbar[ACTION_LBALL].extra_binds[1].key = 'w';
	hotbar[ACTION_LBALL].extra_binds[1].modifiers = INPUT_MOD_CTRL;
	hotbar[ACTION_LBALL].extra_binds[1].target_override = HOTBAR_TGT_CHR;
	hotbar[ACTION_LBALL].extra_binds[1].cast_override = HOTBAR_CAST_DEFAULT;
	hotbar[ACTION_LBALL].extra_bind_count = 2;

	visible_slots = num < HOTBAR_SLOTS_PER_ROW ? num : HOTBAR_SLOTS_PER_ROW;
	init_dots();
}

DLL_EXPORT void input_load_legacy_defaults(void)
{
	input_reset_all();
	input_rebind("ui.cancel", SDLK_ESCAPE, INPUT_MOD_NONE);
	input_rebind("ui.menu", SDLK_F10, INPUT_MOD_NONE);
	input_rebind("move.up", SDLK_UNKNOWN, INPUT_MOD_NONE);
	input_rebind("move.down", SDLK_UNKNOWN, INPUT_MOD_NONE);
	input_rebind("move.left", SDLK_UNKNOWN, INPUT_MOD_NONE);
	input_rebind("move.right", SDLK_UNKNOWN, INPUT_MOD_NONE);
	hotbar_setup_legacy_defaults();
}

/* ── Action-bar compatibility (stubs — legacy action bar removed) ──── */

SDL_Keycode input_action_slot_key(int slot, int row)
{
	(void)slot;
	(void)row;
	return SDLK_UNKNOWN;
}

int input_key_to_action_slot(SDL_Keycode key)
{
	(void)key;
	return -1;
}

int input_action_slot_available(int slot)
{
	extern int *action_skill;
	if (slot < 0 || slot >= MAXACTIONSLOT) {
		return 0;
	}
	int skill = action_skill[slot];
	if (skill == -1) {
		return 1; /* always available (look, map) */
	}
	if (skill == -2) {
		return 0; /* disabled for this server version */
	}
	return value[0][skill] != 0;
}

/* ── Key name utilities ────────────────────────────────────────────────── */

static char key_str_buf[128];

DLL_EXPORT const char *input_key_to_string(SDL_Keycode key, Uint8 modifiers)
{
	if (key == SDLK_UNKNOWN) {
		return "unbound";
	}

	key_str_buf[0] = '\0';
	size_t pos = 0;
	if (modifiers & INPUT_MOD_CTRL) {
		pos += (size_t)snprintf(key_str_buf + pos, sizeof(key_str_buf) - pos, "ctrl+");
	}
	if (modifiers & INPUT_MOD_SHIFT) {
		pos += (size_t)snprintf(key_str_buf + pos, sizeof(key_str_buf) - pos, "shift+");
	}
	if (modifiers & INPUT_MOD_ALT) {
		pos += (size_t)snprintf(key_str_buf + pos, sizeof(key_str_buf) - pos, "alt+");
	}

	if (key == INPUT_MOUSE_X1) {
		snprintf(key_str_buf + pos, sizeof(key_str_buf) - pos, "mouse4");
		return key_str_buf;
	}
	if (key == INPUT_MOUSE_X2) {
		snprintf(key_str_buf + pos, sizeof(key_str_buf) - pos, "mouse5");
		return key_str_buf;
	}

	const char *name = SDL_GetKeyName(key);
	if (name && name[0]) {
		size_t off = strlen(key_str_buf);
		size_t len = strlen(name);
		if (off + len < sizeof(key_str_buf) - 1) {
			for (size_t i = 0; i < len; i++) {
				key_str_buf[off + i] = (char)tolower((unsigned char)name[i]);
			}
			key_str_buf[off + len] = '\0';
		}
	}
	return key_str_buf;
}

DLL_EXPORT int input_string_to_key(const char *str, SDL_Keycode *out_key, Uint8 *out_modifiers)
{
	if (!str || !out_key || !out_modifiers) {
		return -1;
	}

	*out_key = SDLK_UNKNOWN;
	*out_modifiers = INPUT_MOD_NONE;

	if (strcmp(str, "unbound") == 0 || str[0] == '\0') {
		return 0;
	}

	char buf[128];
	snprintf(buf, sizeof(buf), "%s", str);

	/* parse modifier prefixes */
	char *p = buf;
	for (;;) {
		if (strncmp(p, "ctrl+", 5) == 0) {
			*out_modifiers |= INPUT_MOD_CTRL;
			p += 5;
		} else if (strncmp(p, "shift+", 6) == 0) {
			*out_modifiers |= INPUT_MOD_SHIFT;
			p += 6;
		} else if (strncmp(p, "alt+", 4) == 0) {
			*out_modifiers |= INPUT_MOD_ALT;
			p += 4;
		} else {
			break;
		}
	}

	if (strcmp(p, "mouse4") == 0) {
		*out_key = INPUT_MOUSE_X1;
		return 0;
	}
	if (strcmp(p, "mouse5") == 0) {
		*out_key = INPUT_MOUSE_X2;
		return 0;
	}

	SDL_Keycode key = SDL_GetKeyFromName(p);
	if (key != SDLK_UNKNOWN) {
		*out_key = key;
		return 0;
	}

	p[0] = (char)toupper((unsigned char)p[0]);
	key = SDL_GetKeyFromName(p);
	if (key != SDLK_UNKNOWN) {
		*out_key = key;
		return 0;
	}

	for (int i = 1; p[i]; i++) {
		p[i] = (char)toupper((unsigned char)p[i]);
	}
	key = SDL_GetKeyFromName(p);
	if (key != SDLK_UNKNOWN) {
		*out_key = key;
		return 0;
	}

	/* single character */
	if (p[0] && !p[1]) {
		*out_key = (SDL_Keycode)tolower((unsigned char)p[0]);
		return 0;
	}

	return -1;
}

/* ── JSON config ───────────────────────────────────────────────────────── */

int input_load_config(const char *path)
{
	if (!path) {
		return -1;
	}

	FILE *fp = fopen(path, "rb");
	if (!fp) {
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (size <= 0 || size > 1024 * 1024) {
		addline("keybinds: %s has invalid size %ld, ignoring", path, size);
		fclose(fp);
		return -1;
	}

	char *data = xmalloc((size_t)size + 1, MEM_TEMP);
	if (!data) {
		fclose(fp);
		return -1;
	}
	size_t nread = fread(data, 1, (size_t)size, fp);
	fclose(fp);
	if (nread == 0) {
		xfree(data);
		return -1;
	}
	data[nread] = '\0';

	cJSON *root = cJSON_Parse(data);
	xfree(data);
	if (!root) {
		addline("keybinds: failed to parse %s (corrupt JSON), backing up", path);
		char bak[512];
		snprintf(bak, sizeof(bak), "%s.corrupt", path);
		rename(path, bak);
		return -1;
	}

	/* load non-default key overrides */
	cJSON *jbinds = cJSON_GetObjectItem(root, "bindings");
	if (jbinds && cJSON_IsObject(jbinds)) {
		cJSON *entry;
		cJSON_ArrayForEach(entry, jbinds)
		{
			if (!cJSON_IsString(entry)) {
				continue;
			}
			InputBinding *b = input_find_by_id(entry->string);
			if (!b || !b->rebindable) {
				continue;
			}

			SDL_Keycode key;
			Uint8 mods;
			if (input_string_to_key(entry->valuestring, &key, &mods) == 0) {
				b->key = key;
				b->modifiers = mods;
			}
		}
	}

	/* load misc settings */
	cJSON *jsettings = cJSON_GetObjectItem(root, "settings");
	if (jsettings && cJSON_IsObject(jsettings)) {
		cJSON *v;
		v = cJSON_GetObjectItem(jsettings, "action_enabled");
		if (v && cJSON_IsBool(v)) {
			action_enabled = cJSON_IsTrue(v) ? 1 : 0;
		}
		v = cJSON_GetObjectItem(jsettings, "gear_lock");
		if (v && cJSON_IsBool(v)) {
			gear_lock = cJSON_IsTrue(v) ? 1 : 0;
		}
		v = cJSON_GetObjectItem(jsettings, "cast_mode");
		if (v && cJSON_IsNumber(v)) {
			hotbar_set_cast_mode((int)cJSON_GetNumberValue(v));
		}
		v = cJSON_GetObjectItem(jsettings, "show_hotkeys");
		if (v && cJSON_IsBool(v)) {
			show_hotkeys = cJSON_IsTrue(v) ? 1 : 0;
		}
		v = cJSON_GetObjectItem(jsettings, "show_names");
		if (v && cJSON_IsBool(v)) {
			show_names = cJSON_IsTrue(v) ? 1 : 0;
		}
		v = cJSON_GetObjectItem(jsettings, "hotbar_rows");
		if (v && cJSON_IsNumber(v)) {
			hotbar_set_rows((int)cJSON_GetNumberValue(v));
		}
	}

	/* load hotbar: each entry is an object {"type":"item","item_type":N}
	 * or {"type":"spell","action_slot":N,"spell_cmd":N,"spell_target":N} */
	cJSON *jhotbar = cJSON_GetObjectItem(root, "hotbar");
	if (jhotbar && cJSON_IsArray(jhotbar)) {
		int slot = 0;
		cJSON *jslot;
		cJSON_ArrayForEach(jslot, jhotbar)
		{
			if (slot >= HOTBAR_MAX_SLOTS) {
				break;
			}
			memset(&hotbar[slot], 0, sizeof(hotbar[slot]));

			if (cJSON_IsObject(jslot)) {
				cJSON *jtype = cJSON_GetObjectItem(jslot, "type");
				const char *tstr = (jtype && cJSON_IsString(jtype)) ? jtype->valuestring : "";

				if (strcmp(tstr, "item") == 0) {
					cJSON *jit = cJSON_GetObjectItem(jslot, "item_type");
					uint32_t itype = (jit && cJSON_IsNumber(jit)) ? (uint32_t)jit->valueint : 0;
					hotbar[slot].type = HOTBAR_ITEM;
					hotbar[slot].item_type = itype;
					hotbar[slot].inv_index = find_item_in_inventory(itype);
				} else if (strcmp(tstr, "spell") == 0) {
					cJSON *jsp = cJSON_GetObjectItem(jslot, "spell");
					int asp = (jsp && cJSON_IsNumber(jsp)) ? jsp->valueint : -1;
					if (asp < 0 || asp >= ACTION_COUNT) {
						addline("keybinds: hotbar slot %d has invalid spell %d, skipping", slot, asp);
						slot++;
						continue;
					}
					hotbar[slot].type = HOTBAR_SPELL;
					hotbar[slot].action_slot = asp;

					cJSON *jpt = cJSON_GetObjectItem(jslot, "target");
					if (jpt && cJSON_IsString(jpt)) {
						const char *ts = cJSON_GetStringValue(jpt);
						if (strcmp(ts, "map") == 0) {
							hotbar[slot].primary_target = HOTBAR_TGT_MAP;
						} else if (strcmp(ts, "chr") == 0) {
							hotbar[slot].primary_target = HOTBAR_TGT_CHR;
						} else if (strcmp(ts, "self") == 0) {
							hotbar[slot].primary_target = HOTBAR_TGT_SELF;
						}
					}
				}

				/* extra binds (optional) */
				cJSON *jbinds_arr = cJSON_GetObjectItem(jslot, "binds");
				if (jbinds_arr && cJSON_IsArray(jbinds_arr)) {
					int bi = 0;
					cJSON *jb;
					cJSON_ArrayForEach(jb, jbinds_arr)
					{
						if (bi >= HOTBAR_MAX_BINDS) {
							break;
						}
						cJSON *jk = cJSON_GetObjectItem(jb, "key");
						cJSON *jc = cJSON_GetObjectItem(jb, "cast");
						cJSON *jt = cJSON_GetObjectItem(jb, "target");
						if (!jk || !cJSON_IsString(jk)) {
							continue;
						}

						SDL_Keycode bkey;
						Uint8 bmods;
						if (input_string_to_key(cJSON_GetStringValue(jk), &bkey, &bmods) != 0) {
							continue;
						}

						HotbarCastOverride co = HOTBAR_CAST_DEFAULT;
						if (jc && cJSON_IsString(jc)) {
							const char *cs = cJSON_GetStringValue(jc);
							if (strcmp(cs, "normal") == 0) {
								co = HOTBAR_CAST_NORMAL;
							} else if (strcmp(cs, "quick") == 0) {
								co = HOTBAR_CAST_QUICK;
							} else if (strcmp(cs, "indicator") == 0) {
								co = HOTBAR_CAST_INDICATOR;
							}
						}

						HotbarTargetOverride to = HOTBAR_TGT_DEFAULT;
						if (jt && cJSON_IsString(jt)) {
							const char *ts = cJSON_GetStringValue(jt);
							if (strcmp(ts, "map") == 0) {
								to = HOTBAR_TGT_MAP;
							} else if (strcmp(ts, "chr") == 0) {
								to = HOTBAR_TGT_CHR;
							} else if (strcmp(ts, "self") == 0) {
								to = HOTBAR_TGT_SELF;
							}
						}

						hotbar[slot].extra_binds[bi].key = bkey;
						hotbar[slot].extra_binds[bi].modifiers = bmods;
						hotbar[slot].extra_binds[bi].cast_override = co;
						hotbar[slot].extra_binds[bi].target_override = to;
						bi++;
					}
					hotbar[slot].extra_bind_count = bi;
				}
			} else if (cJSON_IsNumber(jslot) && jslot->valueint > 0) {
				/* backward compat: bare number = item_type */
				hotbar[slot].type = HOTBAR_ITEM;
				hotbar[slot].item_type = (uint32_t)jslot->valueint;
				hotbar[slot].inv_index = find_item_in_inventory(hotbar[slot].item_type);
			}
			slot++;
		}
	}

	cJSON_Delete(root);
	return 0;
}

int input_save_config(const char *path)
{
	if (!path) {
		return -1;
	}

	cJSON *root = cJSON_CreateObject();
	if (!root) {
		return -1;
	}

	cJSON_AddNumberToObject(root, "version", 1);

	/* only store bindings that differ from their defaults */
	cJSON *jbinds = cJSON_CreateObject();
	for (int i = 0; i < binding_count; i++) {
		InputBinding *b = &bindings[i];
		if (!b->rebindable) {
			continue;
		}
		if (b->key == b->default_key && b->modifiers == b->default_modifiers) {
			continue;
		}
		cJSON_AddStringToObject(jbinds, b->id, input_key_to_string(b->key, b->modifiers));
	}
	cJSON_AddItemToObject(root, "bindings", jbinds);

	cJSON *jsettings = cJSON_CreateObject();
	cJSON_AddBoolToObject(jsettings, "action_enabled", action_enabled);
	cJSON_AddBoolToObject(jsettings, "gear_lock", gear_lock);
	cJSON_AddNumberToObject(jsettings, "cast_mode", cast_mode);
	cJSON_AddBoolToObject(jsettings, "show_hotkeys", show_hotkeys);
	cJSON_AddBoolToObject(jsettings, "show_names", show_names);
	cJSON_AddNumberToObject(jsettings, "hotbar_rows", active_rows);
	cJSON_AddItemToObject(root, "settings", jsettings);

	/* save hotbar slots */
	cJSON *jhotbar = cJSON_CreateArray();
	for (int i = 0; i < HOTBAR_MAX_SLOTS; i++) {
		cJSON *jslot = cJSON_CreateObject();
		switch (hotbar[i].type) {
		case HOTBAR_ITEM:
			cJSON_AddStringToObject(jslot, "type", "item");
			cJSON_AddNumberToObject(jslot, "item_type", (double)hotbar[i].item_type);
			break;
		case HOTBAR_SPELL: {
			static const char *tgt_names[] = {"default", "map", "chr", "self"};
			cJSON_AddStringToObject(jslot, "type", "spell");
			cJSON_AddNumberToObject(jslot, "spell", hotbar[i].action_slot);
			if (hotbar[i].primary_target != HOTBAR_TGT_DEFAULT) {
				cJSON_AddStringToObject(jslot, "target", tgt_names[hotbar[i].primary_target]);
			}
			break;
		}
		default:
			cJSON_AddStringToObject(jslot, "type", "empty");
			break;
		}
		/* save extra binds if any */
		if (hotbar[i].extra_bind_count > 0) {
			static const char *cast_names[] = {"default", "normal", "quick", "indicator"};
			static const char *tgt_names[] = {"default", "map", "chr", "self"};
			cJSON *jbinds_arr = cJSON_CreateArray();
			for (int b = 0; b < hotbar[i].extra_bind_count; b++) {
				const HotbarBind *hb = &hotbar[i].extra_binds[b];
				cJSON *jb = cJSON_CreateObject();
				cJSON_AddStringToObject(jb, "key", input_key_to_string(hb->key, hb->modifiers));
				cJSON_AddStringToObject(jb, "cast", cast_names[hb->cast_override]);
				cJSON_AddStringToObject(jb, "target", tgt_names[hb->target_override]);
				cJSON_AddItemToArray(jbinds_arr, jb);
			}
			cJSON_AddItemToObject(jslot, "binds", jbinds_arr);
		}
		cJSON_AddItemToArray(jhotbar, jslot);
	}
	cJSON_AddItemToObject(root, "hotbar", jhotbar);

	char *json = cJSON_Print(root);
	cJSON_Delete(root);
	if (!json) {
		return -1;
	}

	FILE *fp = fopen(path, "w");
	if (!fp) {
		cJSON_free(json);
		return -1;
	}
	fputs(json, fp);
	fclose(fp);
	cJSON_free(json);
	return 0;
}

/* ── Binary config migration (moac.dat / moac35.dat) ───────────────────── */

int input_migrate_binary_config(const char *path)
{
	if (!path) {
		return -1;
	}

	FILE *fp = fopen(path, "rb");
	if (!fp) {
		return -1;
	}

	SDL_Keycode old_user_keys[10];
	char old_action_row[2][MAXACTIONSLOT];
	int old_action_enabled, old_gear_lock;

	if (fread(&old_user_keys, sizeof(old_user_keys), 1, fp) != 1 ||
	    fread(&old_action_row, sizeof(old_action_row), 1, fp) != 1 ||
	    fread(&old_action_enabled, sizeof(old_action_enabled), 1, fp) != 1 ||
	    fread(&old_gear_lock, sizeof(old_gear_lock), 1, fp) != 1) {
		fclose(fp);
		return -1;
	}
	fclose(fp);

	action_enabled = old_action_enabled;
	gear_lock = old_gear_lock;

	/* map old action_row[0] slots → combat bindings */
	static const char *combat_ids[MAXACTIONSLOT] = {"action.attack", "action.fireball", "action.lball", NULL, NULL,
	    NULL, "action.bless", "action.heal", NULL, NULL, NULL, "action.takegive", NULL, "action.look"};
	/* map old action_row[1] slots → spell bindings */
	static const char *spell_ids[MAXACTIONSLOT] = {NULL, "spell.fireball", "spell.lball", "spell.flash", "spell.freeze",
	    "spell.shield", "spell.bless", "spell.heal", "spell.warcry", "spell.pulse", "spell.firering", NULL, "spell.map",
	    NULL};

	for (int i = 0; i < MAXACTIONSLOT; i++) {
		if (combat_ids[i] && old_action_row[0][i] >= 'a' && old_action_row[0][i] <= 'z') {
			InputBinding *b = input_find_by_id(combat_ids[i]);
			if (b && b->rebindable) {
				b->key = (SDL_Keycode)old_action_row[0][i];
			}
		}
		if (spell_ids[i] && old_action_row[1][i] >= 'a' && old_action_row[1][i] <= 'z') {
			InputBinding *b = input_find_by_id(spell_ids[i]);
			if (b && b->rebindable) {
				b->key = (SDL_Keycode)old_action_row[1][i];
			}
		}
	}

	/* rename old file */
	char bak[512];
	snprintf(bak, sizeof(bak), "%s.bak", path);
	rename(path, bak);

	return 0;
}
