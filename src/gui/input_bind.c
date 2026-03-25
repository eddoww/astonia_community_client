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

const char *input_category_name(InputCategory cat)
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

static void on_set_speed(InputBinding *self)
{
	cmd_speed(self->param);
}

static void on_action_mode_on(InputBinding *self)
{
	(void)self;
	if (!context_key_isset()) {
		context_action_enable(1);
	}
}

static void on_action_mode_off(InputBinding *self)
{
	(void)self;
	if (!context_key_isset()) {
		context_action_enable(0);
	}
}

/* forward declaration for item scanning */
static int find_item_in_inventory(uint32_t item_type);

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

int hotbar_visible_slots(void)
{
	return visible_slots;
}

int hotbar_rows(void)
{
	return active_rows;
}

void hotbar_set_rows(int count)
{
	if (count < 1) {
		count = 1;
	}
	if (count > HOTBAR_MAX_ROWS) {
		count = HOTBAR_MAX_ROWS;
	}
	active_rows = count;
}

void hotbar_set_visible_slots(int count)
{
	if (count < 1) {
		count = 1;
	}
	if (count > HOTBAR_MAX_SLOTS) {
		count = HOTBAR_MAX_SLOTS;
	}
	visible_slots = count;
}

int hotbar_cast_mode(void)
{
	return cast_mode;
}

void hotbar_set_cast_mode(int mode)
{
	if (mode >= CAST_NORMAL && mode <= CAST_QUICK_INDICATOR) {
		cast_mode = mode;
	}
}

int hotbar_show_hotkeys(void)
{
	return show_hotkeys;
}

void hotbar_set_show_hotkeys(int on)
{
	show_hotkeys = on ? 1 : 0;
}

int hotbar_show_names(void)
{
	return show_names;
}

void hotbar_set_show_names(int on)
{
	show_names = on ? 1 : 0;
}

void hotbar_assign_item(int slot, int inventory_index)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}
	memset(&hotbar[slot], 0, sizeof(hotbar[slot]));
	hotbar[slot].type = HOTBAR_ITEM;
	hotbar[slot].inv_index = inventory_index;
	hotbar[slot].item_type = (inventory_index > 0) ? item[inventory_index] : 0;
}

void hotbar_assign_item_by_type(int slot, uint32_t item_type)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}
	memset(&hotbar[slot], 0, sizeof(hotbar[slot]));
	hotbar[slot].type = HOTBAR_ITEM;
	hotbar[slot].item_type = item_type;
	hotbar[slot].inv_index = find_item_in_inventory(item_type);
}

void hotbar_assign_spell(int slot, int action_slot)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}
	memset(&hotbar[slot], 0, sizeof(hotbar[slot]));
	hotbar[slot].type = HOTBAR_SPELL;
	hotbar[slot].action_slot = action_slot;
}

void hotbar_clear(int slot)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}
	memset(&hotbar[slot], 0, sizeof(hotbar[slot]));
}

void hotbar_clear_all(void)
{
	memset(hotbar, 0, sizeof(hotbar));
}

/* set up default hotbar contents for first-time users (no saved config) */
void hotbar_setup_defaults(void)
{
	/* spell layout: covers both mage and warrior spells.
	 * spells the player doesn't have will be inactive but visible. */
	static const struct {
		int action_slot;
		int has_quick; /* 1 = add shift+key extra bind for quick cast */
	} spell_defaults[] = {
	    {ACTION_ATTACK, 0}, /* slot 1 */
	    {ACTION_FIREBALL, 1}, /* slot 2 — shift+2 = quick cast */
	    {ACTION_LBALL, 1}, /* slot 3 — shift+3 = quick cast */
	    {ACTION_FLASH, 0}, /* slot 4 — self-cast (always instant) */
	    {ACTION_FREEZE, 0}, /* slot 5 — self-cast */
	    {ACTION_BLESS, 1}, /* slot 6 — shift+6 = quick cast */
	    {ACTION_SHIELD, 0}, /* slot 7 — self-cast */
	    {ACTION_HEAL, 1}, /* slot 8 — shift+8 = quick cast */
	    {ACTION_WARCRY, 0}, /* slot 9 — self-cast */
	    {ACTION_PULSE, 0}, /* slot 0 — self-cast */
	    {ACTION_FIRERING, 0}, /* slot Q — self-cast */
	    {-1, 0}, /* slot W — empty (potions) */
	    {ACTION_TAKEGIVE, 0}, /* slot E */
	    {ACTION_MAP, 0}, /* slot R */
	    {ACTION_LOOK, 0}, /* slot T */
	};

	int num_defaults = (int)(sizeof(spell_defaults) / sizeof(spell_defaults[0]));
	for (int i = 0; i < num_defaults; i++) {
		if (spell_defaults[i].action_slot < 0) {
			continue; /* leave slot empty */
		}
		hotbar_assign_spell(i, spell_defaults[i].action_slot);

		/* add shift+key quick cast extra bind for dual-target spells */
		if (spell_defaults[i].has_quick && i < HOTBAR_SLOTS_PER_ROW) {
			hotbar_add_bind(i, row1_keys[i], INPUT_MOD_SHIFT, HOTBAR_CAST_QUICK, HOTBAR_TGT_DEFAULT);
		}
	}
}

const HotbarSlot *hotbar_get(int slot)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return NULL;
	}
	return &hotbar[slot];
}

uint32_t hotbar_slot_sprite(int slot)
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
		return (uint32_t)(800 + hotbar[slot].action_slot); /* spell icon sprites */
	default:
		return 0;
	}
}

const char *hotbar_slot_name(int slot)
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
	for (int i = 30; i < _inventorysize; i++) {
		if (item[i] == item_type && (item_flags[i] & IF_USE)) {
			return i;
		}
	}
	return 0;
}

void hotbar_on_item_changed(int inventory_index)
{
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

/* ── Per-spell capability queries ───────────────────────────────────── */

int hotbar_spell_valid_targets(int action_slot)
{
	switch (action_slot) {
	/* self-only — no target choice, always fires on self */
	case ACTION_FLASH:
	case ACTION_FREEZE:
	case ACTION_SHIELD:
	case ACTION_WARCRY:
	case ACTION_PULSE:
	case ACTION_FIRERING:
		return HOTBAR_VTGT_SELF;

	/* character or self */
	case ACTION_BLESS:
	case ACTION_HEAL:
		return HOTBAR_VTGT_CHR | HOTBAR_VTGT_SELF;

	/* character or map */
	case ACTION_FIREBALL:
	case ACTION_LBALL:
		return HOTBAR_VTGT_CHR | HOTBAR_VTGT_MAP;

	/* character only */
	case ACTION_ATTACK:
		return HOTBAR_VTGT_CHR;

	/* context-dependent (take/give/use/drop) */
	case ACTION_TAKEGIVE:
		return HOTBAR_VTGT_CHR | HOTBAR_VTGT_MAP;

	/* look at anything */
	case ACTION_LOOK:
		return HOTBAR_VTGT_CHR | HOTBAR_VTGT_MAP;

	/* toggle (map) — no targeting */
	case ACTION_MAP:
		return 0;

	default:
		return 0;
	}
}

int hotbar_spell_has_cast_modes(int action_slot)
{
	switch (action_slot) {
	/* self-only spells — always instant, no mode choice */
	case ACTION_FLASH:
	case ACTION_FREEZE:
	case ACTION_SHIELD:
	case ACTION_WARCRY:
	case ACTION_PULSE:
	case ACTION_FIRERING:
	case ACTION_MAP:
		return 0;
	default:
		return 1;
	}
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
		/* instant: cast at whatever is under cursor right now */
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
static void hotbar_activate_with_mode(int slot, int mode)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}

	switch (hotbar[slot].type) {
	case HOTBAR_ITEM:
		hotbar_try_use_item(slot);
		break;
	case HOTBAR_SPELL:
		hotbar_cast_spell(slot, resolve_spell_action(hotbar[slot].action_slot), mode);
		break;
	default:
		break;
	}
}

/* activate a hotbar slot from a mouse click — always uses normal cast */
void hotbar_activate(int slot)
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

	b = reg("sys.escape", "Cancel", INPUT_CAT_SYSTEM, SDLK_ESCAPE, 0, on_escape);
	if (b) {
		b->rebindable = 0;
	}

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

	reg("move.action_on", "Enable Action Mode", INPUT_CAT_MOVEMENT, '+', 0, on_action_mode_on);
	reg("move.action_on_alt", "Enable Action Mode (Alt)", INPUT_CAT_MOVEMENT, '=', 0, on_action_mode_on);
	reg("move.action_off", "Disable Action Mode", INPUT_CAT_MOVEMENT, '-', 0, on_action_mode_off);

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

void input_shutdown(void)
{
	binding_count = 0;
}

/* ── Modifier helper ───────────────────────────────────────────────────── */

Uint8 input_current_modifiers(void)
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

InputBinding *input_find(SDL_Keycode key, Uint8 modifiers)
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

InputBinding *input_find_by_id(const char *id)
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

void input_execute(InputBinding *b)
{
	if (b && b->on_press) {
		b->last_used = now;
		b->on_press(b);
	}
}

/* ── Queries ───────────────────────────────────────────────────────────── */

SDL_Keycode input_get_key(const char *id)
{
	InputBinding *b = input_find_by_id(id);
	return b ? b->key : SDLK_UNKNOWN;
}

int input_binding_count(void)
{
	return binding_count;
}

InputBinding *input_binding_at(int index)
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

int input_rebind(const char *id, SDL_Keycode key, Uint8 modifiers)
{
	InputBinding *b = input_find_by_id(id);
	if (!b) {
		return -1;
	}
	if (!b->rebindable) {
		return -2;
	}

	/* auto-clear any conflicting binding */
	InputBinding *conflict = input_find_conflict(key, modifiers, id);
	if (conflict && conflict->rebindable) {
		conflict->key = SDLK_UNKNOWN;
		conflict->modifiers = INPUT_MOD_NONE;
	}

	b->key = key;
	b->modifiers = modifiers;
	return 0;
}

int input_unbind(const char *id)
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

void input_reset_one(const char *id)
{
	InputBinding *b = input_find_by_id(id);
	if (b) {
		b->key = b->default_key;
		b->modifiers = b->default_modifiers;
	}
}

void input_reset_all(void)
{
	for (int i = 0; i < binding_count; i++) {
		bindings[i].key = bindings[i].default_key;
		bindings[i].modifiers = bindings[i].default_modifiers;
	}
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

const char *input_key_to_string(SDL_Keycode key, Uint8 modifiers)
{
	if (key == SDLK_UNKNOWN) {
		return "unbound";
	}

	key_str_buf[0] = '\0';
	if (modifiers & INPUT_MOD_CTRL) {
		strcat(key_str_buf, "ctrl+");
	}
	if (modifiers & INPUT_MOD_SHIFT) {
		strcat(key_str_buf, "shift+");
	}
	if (modifiers & INPUT_MOD_ALT) {
		strcat(key_str_buf, "alt+");
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

int input_string_to_key(const char *str, SDL_Keycode *out_key, Uint8 *out_modifiers)
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

	/* try SDL lookup as-is, then capitalize first letter (e.g. "escape" → "Escape"),
	 * then all uppercase (e.g. "f1" → "F1"). SDL_GetKeyFromName is case-sensitive. */
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
		fclose(fp);
		return -1;
	}

	char *data = xmalloc((size_t)size + 1, MEM_TEMP);
	if (!data) {
		fclose(fp);
		return -1;
	}
	fread(data, 1, (size_t)size, fp);
	data[size] = '\0';
	fclose(fp);

	cJSON *root = cJSON_Parse(data);
	xfree(data);
	if (!root) {
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
					hotbar[slot].type = HOTBAR_SPELL;
					hotbar[slot].action_slot = (jsp && cJSON_IsNumber(jsp)) ? jsp->valueint : 0;
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
		case HOTBAR_SPELL:
			cJSON_AddStringToObject(jslot, "type", "spell");
			cJSON_AddNumberToObject(jslot, "spell", hotbar[i].action_slot);
			break;
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

	size_t ok = 0;
	ok += fread(&old_user_keys, sizeof(old_user_keys), 1, fp);
	ok += fread(&old_action_row, sizeof(old_action_row), 1, fp);
	ok += fread(&old_action_enabled, sizeof(old_action_enabled), 1, fp);
	ok += fread(&old_gear_lock, sizeof(old_gear_lock), 1, fp);
	fclose(fp);
	if (ok != 4) {
		return -1;
	}

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
