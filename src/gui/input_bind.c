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

/* ── Hotbar ─────────────────────────────────────────────────────────────
 *
 * Each slot holds either an item or a spell. Item slots auto-refill from
 * inventory when consumed. Spell slots activate the action bar system.
 */

static HotbarSlot hotbar[HOTBAR_MAX_SLOTS];
static int visible_slots = HOTBAR_DEFAULT_SLOTS;

int hotbar_visible_slots(void)
{
	return visible_slots;
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

void hotbar_assign_spell(int slot, int action_slot, int spell_cmd, int spell_target)
{
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}
	memset(&hotbar[slot], 0, sizeof(hotbar[slot]));
	hotbar[slot].type = HOTBAR_SPELL;
	hotbar[slot].action_slot = action_slot;
	hotbar[slot].spell_cmd = spell_cmd;
	hotbar[slot].spell_target = spell_target;
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
		if (hotbar[s].type != HOTBAR_ITEM) {
			continue;
		}
		if (hotbar[s].inv_index != inventory_index) {
			continue;
		}
		if (hotbar[s].item_type == 0) {
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

static void on_hotbar_press(InputBinding *self)
{
	int slot = self->param;
	if (slot < 0 || slot >= HOTBAR_MAX_SLOTS) {
		return;
	}

	switch (hotbar[slot].type) {
	case HOTBAR_ITEM: {
		int inv_idx = hotbar[slot].inv_index;
		if (inv_idx > 0 && item[inv_idx]) {
			cmd_use_inv(inv_idx);
		}
		break;
	}
	case HOTBAR_SPELL:
		/* spells with a command cast directly; actions activate the action bar */
		if (hotbar[slot].spell_cmd > 0) {
			switch (hotbar[slot].spell_target) {
			case TGT_MAP:
				exec_cmd(CMD_MAP_CAST_K, hotbar[slot].spell_cmd);
				break;
			case TGT_CHR:
				exec_cmd(CMD_CHR_CAST_K, hotbar[slot].spell_cmd);
				break;
			case TGT_SLF:
				exec_cmd(CMD_SLF_CAST_K, hotbar[slot].spell_cmd);
				break;
			default:
				break;
			}
		} else {
			/* non-spell action (attack, look, take/give) — activate via context */
			context_keydown(self->key);
		}
		break;
	default:
		break;
	}
}

/* action bar (combat + spells) — delegates to existing context system */

static void on_action_key(InputBinding *self)
{
	context_keydown(self->key);
}

/* keytab spells (modifier + number key) */

static void on_cast_spell(InputBinding *self)
{
	int spell = self->param;
	if (spell <= 0) {
		return;
	}

	switch (self->action_slot) { /* action_slot repurposed as target type */
	case TGT_MAP:
		exec_cmd(CMD_MAP_CAST_K, spell);
		break;
	case TGT_CHR:
		exec_cmd(CMD_CHR_CAST_K, spell);
		break;
	case TGT_SLF:
		exec_cmd(CMD_SLF_CAST_K, spell);
		break;
	default:
		break;
	}
	self->last_used = now;
}

/* ── Default bindings ──────────────────────────────────────────────────
 *
 * Skill constants differ between v3 and v35. We pick the right one at
 * registration time with a simple ternary. The (int) casts avoid
 * -Wenum-compare-conditional between v3_t and v35_t.
 */

#define V3_OR_V35(v3_skill, v35_skill) (sv_ver == 35 ? (int)(v35_skill) : (int)(v3_skill))

static void register_all(int sv_ver)
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

	/* ── Hotbar (10 drag-and-drop item slots) ─────────────────────── */

	static const struct {
		const char *id;
		const char *name;
		SDL_Keycode key;
	} hotbar_defaults[HOTBAR_MAX_SLOTS] = {
	    {"hotbar.0", "Hotbar Slot 1", SDLK_F1},
	    {"hotbar.1", "Hotbar Slot 2", SDLK_F2},
	    {"hotbar.2", "Hotbar Slot 3", SDLK_F3},
	    {"hotbar.3", "Hotbar Slot 4", SDLK_F4},
	    {"hotbar.4", "Hotbar Slot 5", SDLK_F5},
	    {"hotbar.5", "Hotbar Slot 6", SDLK_F6},
	    {"hotbar.6", "Hotbar Slot 7", SDLK_F7},
	    {"hotbar.7", "Hotbar Slot 8", SDLK_F8},
	    {"hotbar.8", "Hotbar Slot 9", SDLK_F9},
	    {"hotbar.9", "Hotbar Slot 10", SDLK_F10},
	    {"hotbar.10", "Hotbar Slot 11", SDLK_UNKNOWN},
	    {"hotbar.11", "Hotbar Slot 12", SDLK_UNKNOWN},
	    {"hotbar.12", "Hotbar Slot 13", SDLK_UNKNOWN},
	    {"hotbar.13", "Hotbar Slot 14", SDLK_UNKNOWN},
	    {"hotbar.14", "Hotbar Slot 15", SDLK_UNKNOWN},
	};

	for (int i = 0; i < HOTBAR_MAX_SLOTS; i++) {
		b = reg(hotbar_defaults[i].id, hotbar_defaults[i].name, INPUT_CAT_HOTBAR, hotbar_defaults[i].key, 0,
		    on_hotbar_press);
		if (b) {
			b->param = i;
		}
	}

	/* ── Combat — action bar row 0 (target mode) ──────────────────── */

#define COMBAT(id, name, key, slot, skill)                                                                             \
	do {                                                                                                               \
		b = reg(id, name, INPUT_CAT_COMBAT, key, 0, on_action_key);                                                    \
		if (b) {                                                                                                       \
			b->action_slot = (slot);                                                                                   \
			b->required_skill = (skill);                                                                               \
		}                                                                                                              \
	} while (0)

	COMBAT("action.attack", "Attack", 'a', ACTION_ATTACK, V3_OR_V35(V3_PERCEPT, V35_PERCEPT));
	COMBAT("action.fireball", "Fireball", 's', ACTION_FIREBALL, V3_OR_V35(V3_FIREBALL, V35_FIRE));
	COMBAT("action.lball", "Lightning Ball", 'd', ACTION_LBALL, V3_OR_V35(V3_FLASH, V35_FLASH));
	COMBAT("action.bless", "Bless", sv_ver == 35 ? SDLK_UNKNOWN : 'f', ACTION_BLESS, V3_OR_V35(V3_BLESS, V35_BLESS));
	COMBAT("action.heal", "Heal", sv_ver == 35 ? 'b' : 'g', ACTION_HEAL, V3_OR_V35(V3_HEAL, V35_HEAL));
	COMBAT("action.takegive", "Take/Use/Give/Drop", sv_ver == 35 ? 'g' : 'h', ACTION_TAKEGIVE,
	    V3_OR_V35(V3_PERCEPT, V35_PERCEPT));
	COMBAT("action.look", "Look", 'l', ACTION_LOOK, -1);

#undef COMBAT

	/* ── Spells — action bar row 1 (self/map cast) ────────────────── */

#define SPELL(id, name, key, slot, skill)                                                                              \
	do {                                                                                                               \
		b = reg(id, name, INPUT_CAT_SPELLS, key, 0, on_action_key);                                                    \
		if (b) {                                                                                                       \
			b->action_slot = (slot);                                                                                   \
			b->required_skill = (skill);                                                                               \
		}                                                                                                              \
	} while (0)

	SPELL("spell.fireball", "Fireball (Map)", 'q', ACTION_FIREBALL, V3_OR_V35(V3_FIREBALL, V35_FIRE));
	SPELL("spell.lball", "Lightning Ball (Map)", 'w', ACTION_LBALL, V3_OR_V35(V3_FLASH, V35_FLASH));
	SPELL("spell.flash", "Flash", 'e', ACTION_FLASH, V3_OR_V35(V3_FLASH, V35_FLASH));
	SPELL("spell.freeze", "Freeze", 'r', ACTION_FREEZE, V3_OR_V35(V3_FREEZE, V35_FREEZE));
	SPELL("spell.shield", "Magic Shield", 't', ACTION_SHIELD, V3_OR_V35(V3_MAGICSHIELD, V35_MAGICSHIELD));
	SPELL("spell.bless", "Bless (Self)", 'z', ACTION_BLESS, V3_OR_V35(V3_BLESS, V35_BLESS));
	SPELL("spell.heal", "Heal (Self)", 'u', ACTION_HEAL, V3_OR_V35(V3_HEAL, V35_HEAL));
	SPELL("spell.warcry", "Warcry", 'i', ACTION_WARCRY, V3_OR_V35(V3_WARCRY, V35_WARCRY));

	b = reg("spell.pulse", "Pulse", INPUT_CAT_SPELLS, 'o', 0, on_action_key);
	if (b) {
		b->action_slot = ACTION_PULSE;
		b->required_skill = sv_ver == 35 ? -2 : (int)V3_PULSE; /* disabled on v35 */
		if (sv_ver == 35) {
			b->version_mask = INPUT_V3;
		}
	}

	SPELL("spell.firering", "Firering", 'p', ACTION_FIRERING, V3_OR_V35(V3_FIREBALL, V35_FIRE));
	SPELL("spell.map", "Toggle Map", 'm', ACTION_MAP, -1);

#undef SPELL

	/* ── Keytab spells (Ctrl+N = target char, Alt+N = target map) ── */

#define KEYTAB(id, name, key, mods, cl_spell, target, skill)                                                           \
	do {                                                                                                               \
		b = reg(id, name, INPUT_CAT_SPELLS, key, mods, on_cast_spell);                                                 \
		if (b) {                                                                                                       \
			b->param = (cl_spell);                                                                                     \
			b->action_slot = (target);                                                                                 \
			b->required_skill = (skill);                                                                               \
		}                                                                                                              \
	} while (0)

	/* Ctrl+1..9 — cast on character */
	KEYTAB(
	    "keytab.1_chr", "Fireball (Char)", '1', INPUT_MOD_CTRL, CL_FIREBALL, TGT_CHR, V3_OR_V35(V3_FIREBALL, V35_FIRE));
	KEYTAB(
	    "keytab.2_chr", "Lightning Ball (Char)", '2', INPUT_MOD_CTRL, CL_BALL, TGT_CHR, V3_OR_V35(V3_FLASH, V35_FLASH));
	KEYTAB("keytab.3_chr", "Flash", '3', INPUT_MOD_CTRL, CL_FLASH, TGT_SLF, V3_OR_V35(V3_FLASH, V35_FLASH));
	KEYTAB("keytab.4_chr", "Freeze", '4', INPUT_MOD_CTRL, CL_FREEZE, TGT_SLF, V3_OR_V35(V3_FREEZE, V35_FREEZE));
	KEYTAB("keytab.5_chr", "Magic Shield", '5', INPUT_MOD_CTRL, CL_MAGICSHIELD, TGT_SLF,
	    V3_OR_V35(V3_MAGICSHIELD, V35_MAGICSHIELD));
	KEYTAB("keytab.6_chr", "Bless", '6', INPUT_MOD_CTRL, CL_BLESS, sv_ver == 35 ? TGT_SLF : TGT_CHR,
	    V3_OR_V35(V3_BLESS, V35_BLESS));
	KEYTAB("keytab.7_chr", "Heal", '7', INPUT_MOD_CTRL, CL_HEAL, TGT_CHR, V3_OR_V35(V3_HEAL, V35_HEAL));
	KEYTAB("keytab.8_chr", "Warcry", '8', INPUT_MOD_CTRL, CL_WARCRY, TGT_SLF, V3_OR_V35(V3_WARCRY, V35_WARCRY));
	KEYTAB("keytab.9_chr", sv_ver == 35 ? "Firering" : "Pulse", '9', INPUT_MOD_CTRL,
	    sv_ver == 35 ? CL_FIREBALL : CL_PULSE, TGT_SLF, V3_OR_V35(V3_PULSE, V35_FIRE));
	if (sv_ver != 35) {
		KEYTAB("keytab.0_chr", "Firering", '0', INPUT_MOD_CTRL, CL_FIREBALL, TGT_SLF, (int)V3_FIREBALL);
	}

	/* Alt+1..9 — cast on map */
	KEYTAB(
	    "keytab.1_map", "Fireball (Map)", '1', INPUT_MOD_ALT, CL_FIREBALL, TGT_MAP, V3_OR_V35(V3_FIREBALL, V35_FIRE));
	KEYTAB(
	    "keytab.2_map", "Lightning Ball (Map)", '2', INPUT_MOD_ALT, CL_BALL, TGT_MAP, V3_OR_V35(V3_FLASH, V35_FLASH));
	KEYTAB("keytab.3_map", "Flash (Self)", '3', INPUT_MOD_ALT, CL_FLASH, TGT_SLF, V3_OR_V35(V3_FLASH, V35_FLASH));
	KEYTAB("keytab.4_map", "Freeze (Self)", '4', INPUT_MOD_ALT, CL_FREEZE, TGT_SLF, V3_OR_V35(V3_FREEZE, V35_FREEZE));
	KEYTAB("keytab.5_map", "Magic Shield (Self)", '5', INPUT_MOD_ALT, CL_MAGICSHIELD, TGT_SLF,
	    V3_OR_V35(V3_MAGICSHIELD, V35_MAGICSHIELD));
	KEYTAB("keytab.6_map", "Bless Self", '6', INPUT_MOD_ALT, CL_BLESS, TGT_SLF, V3_OR_V35(V3_BLESS, V35_BLESS));
	KEYTAB("keytab.7_map", "Heal Self", '7', INPUT_MOD_ALT, CL_HEAL, TGT_SLF, V3_OR_V35(V3_HEAL, V35_HEAL));
	KEYTAB("keytab.8_map", "Warcry (Self)", '8', INPUT_MOD_ALT, CL_WARCRY, TGT_SLF, V3_OR_V35(V3_WARCRY, V35_WARCRY));
	KEYTAB("keytab.9_map", sv_ver == 35 ? "Firering (Self)" : "Pulse (Self)", '9', INPUT_MOD_ALT,
	    sv_ver == 35 ? CL_FIREBALL : CL_PULSE, TGT_SLF, V3_OR_V35(V3_PULSE, V35_FIRE));
	if (sv_ver != 35) {
		KEYTAB("keytab.0_map", "Firering (Self)", '0', INPUT_MOD_ALT, CL_FIREBALL, TGT_SLF, (int)V3_FIREBALL);
	}

#undef KEYTAB
}

#undef V3_OR_V35

/* ── Init / Shutdown ───────────────────────────────────────────────────── */

void input_init(int sv_ver)
{
	memset(bindings, 0, sizeof(bindings));
	binding_count = 0;
	version_mask = (sv_ver == 35) ? INPUT_V35 : INPUT_V3;

	register_all(sv_ver);
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

/* ── Action-bar compatibility ──────────────────────────────────────────── */

SDL_Keycode input_action_slot_key(int slot, int row)
{
	/* row 0 = INPUT_CAT_COMBAT (on_action_key with action_slot==slot)
	 * row 1 = INPUT_CAT_SPELLS */
	InputCategory cat = (row == 0) ? INPUT_CAT_COMBAT : INPUT_CAT_SPELLS;

	for (int i = 0; i < binding_count; i++) {
		InputBinding *b = &bindings[i];
		if (b->category != cat) {
			continue;
		}
		if (b->action_slot != slot) {
			continue;
		}
		if (!(b->version_mask & version_mask)) {
			continue;
		}
		return b->key;
	}
	return SDLK_UNKNOWN;
}

int input_key_to_action_slot(SDL_Keycode key)
{
	if (key == SDLK_UNKNOWN) {
		return -1;
	}

	for (int i = 0; i < binding_count; i++) {
		InputBinding *b = &bindings[i];
		if (b->key != key) {
			continue;
		}
		if (b->modifiers != INPUT_MOD_NONE) {
			continue;
		}
		if (b->action_slot < 0) {
			continue;
		}
		if (b->on_press != on_action_key) {
			continue;
		}
		if (!(b->version_mask & version_mask)) {
			continue;
		}

		/* check skill requirement */
		if (b->required_skill == -2) {
			continue;
		}
		if (b->required_skill >= 0 && !value[0][b->required_skill]) {
			continue;
		}

		/* row 0 returns slot, row 1 returns slot + 100 */
		return (b->category == INPUT_CAT_SPELLS) ? b->action_slot + 100 : b->action_slot;
	}
	return -1;
}

int input_action_slot_available(int slot)
{
	for (int i = 0; i < binding_count; i++) {
		InputBinding *b = &bindings[i];
		if (b->action_slot != slot) {
			continue;
		}
		if (b->on_press != on_action_key) {
			continue;
		}
		if (!(b->version_mask & version_mask)) {
			continue;
		}

		if (b->required_skill == -1) {
			return 1;
		}
		if (b->required_skill == -2) {
			return 0;
		}
		return value[0][b->required_skill] != 0;
	}
	return 0;
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

	/* try SDL lookup (case-insensitive) */
	SDL_Keycode key = SDL_GetKeyFromName(p);
	if (key != SDLK_UNKNOWN) {
		*out_key = key;
		return 0;
	}

	/* capitalize first letter */
	char cap[128];
	snprintf(cap, sizeof(cap), "%s", p);
	cap[0] = (char)toupper((unsigned char)cap[0]);
	key = SDL_GetKeyFromName(cap);
	if (key != SDLK_UNKNOWN) {
		*out_key = key;
		return 0;
	}

	/* all uppercase (F-keys) */
	for (int i = 0; cap[i]; i++) {
		cap[i] = (char)toupper((unsigned char)cap[i]);
	}
	key = SDL_GetKeyFromName(cap);
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
					cJSON *jas = cJSON_GetObjectItem(jslot, "action_slot");
					cJSON *jsc = cJSON_GetObjectItem(jslot, "spell_cmd");
					cJSON *jst = cJSON_GetObjectItem(jslot, "spell_target");
					hotbar[slot].type = HOTBAR_SPELL;
					hotbar[slot].action_slot = (jas && cJSON_IsNumber(jas)) ? jas->valueint : 0;
					hotbar[slot].spell_cmd = (jsc && cJSON_IsNumber(jsc)) ? jsc->valueint : 0;
					hotbar[slot].spell_target = (jst && cJSON_IsNumber(jst)) ? jst->valueint : 0;
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
			cJSON_AddNumberToObject(jslot, "action_slot", hotbar[i].action_slot);
			cJSON_AddNumberToObject(jslot, "spell_cmd", hotbar[i].spell_cmd);
			cJSON_AddNumberToObject(jslot, "spell_target", hotbar[i].spell_target);
			break;
		default:
			cJSON_AddStringToObject(jslot, "type", "empty");
			break;
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
