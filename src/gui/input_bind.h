/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Unified Input Binding System
 *
 * Every keyboard shortcut in the game is an InputBinding: a key + modifiers
 * mapped to a callback function. The registry holds all bindings, handles
 * lookup, persistence (JSON), and rebinding with conflict detection.
 */

#ifndef INPUT_BIND_H
#define INPUT_BIND_H

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_stdinc.h>
#include "../dll.h"

/* Modifier flags — combinable with bitwise OR */
#define INPUT_MOD_NONE  0
#define INPUT_MOD_SHIFT (1 << 0)
#define INPUT_MOD_CTRL  (1 << 1)
#define INPUT_MOD_ALT   (1 << 2)

/* Virtual keycodes for mouse buttons (outside SDL keycode range) */
#define INPUT_MOUSE_X1 0x40000100
#define INPUT_MOUSE_X2 0x40000101

/* Categories — used to group bindings in the UI */
typedef enum {
	INPUT_CAT_SYSTEM, /* ESC, quit — not rebindable              */
	INPUT_CAT_UI, /* panel toggles, minimap, overlay          */
	INPUT_CAT_COMBAT, /* attack, targeted spells (action bar r0)  */
	INPUT_CAT_SPELLS, /* self/map-cast spells (action bar r1)     */
	INPUT_CAT_MOVEMENT, /* speed modes, action mode toggle          */
	INPUT_CAT_HOTBAR, /* hotbar item slots (drag-and-drop + keys)  */
	INPUT_CAT_COUNT
} InputCategory;

/* Game version mask — which server version(s) a binding applies to */
#define INPUT_V3   (1 << 0)
#define INPUT_V35  (1 << 1)
#define INPUT_VALL (INPUT_V3 | INPUT_V35)

/*
 * InputBinding — one keyboard shortcut.
 *
 * Every binding has a unique string id (e.g. "combat.attack"), a key+modifiers
 * pair, and a callback that fires when the key is pressed. The callback
 * receives the binding itself so it can read .param / .action_slot / etc.
 */
typedef struct InputBinding {
	/* identity */
	const char *id; /* unique, e.g. "combat.attack" — points to static string */
	const char *display_name; /* human-readable, e.g. "Attack" — points to static string */
	InputCategory category;

	/* key assignment */
	SDL_Keycode key;
	Uint8 modifiers;
	SDL_Keycode default_key;
	Uint8 default_modifiers;

	/* what happens when pressed */
	void (*on_press)(struct InputBinding *self);

	/* generic payload — interpreted by on_press callback */
	int param; /* spell id, speed mode, fkey slot, CMD_* constant, etc. */
	int action_slot; /* ACTION_* index for action-bar bindings, -1 otherwise */

	/* requirements */
	int required_skill; /* skill index that must be nonzero; -1=always, -2=disabled */
	int version_mask; /* INPUT_V3 / INPUT_V35 / INPUT_VALL */

	/* runtime */
	Uint64 last_used;
	int rebindable; /* 0 = locked, 1 = user can change */
} InputBinding;

#define INPUT_MAX_BINDINGS 128

/* ── Public API ────────────────────────────────────────────────────────── */

/* lifecycle */
void input_init(int sv_ver);
void input_shutdown(void);

/* lookup & dispatch */
DLL_EXPORT InputBinding *input_find(SDL_Keycode key, Uint8 modifiers);
DLL_EXPORT void input_execute(InputBinding *b);

/* rebinding */
DLL_EXPORT int input_rebind(const char *id, SDL_Keycode key, Uint8 modifiers);
DLL_EXPORT InputBinding *input_register(const char *id, const char *display_name, InputCategory cat, SDL_Keycode key,
    Uint8 modifiers, void (*on_press)(InputBinding *self));
DLL_EXPORT int input_unbind(const char *id);
DLL_EXPORT void input_reset_one(const char *id);
DLL_EXPORT void input_reset_all(void);
DLL_EXPORT void input_load_modern_defaults(void);
DLL_EXPORT void input_load_legacy_defaults(void);
DLL_EXPORT int input_undo_rebind(void);

/* conflict detection — returns conflicting binding, or NULL */
InputBinding *input_find_conflict(SDL_Keycode key, Uint8 modifiers, const char *exclude_id);

/* queries */
DLL_EXPORT InputBinding *input_find_by_id(const char *id);
DLL_EXPORT SDL_Keycode input_get_key(const char *id);
DLL_EXPORT int input_binding_count(void);
DLL_EXPORT InputBinding *input_binding_at(int index);
DLL_EXPORT const char *input_category_name(InputCategory cat);

/* action-bar compatibility */
SDL_Keycode input_action_slot_key(int slot, int row);
int input_key_to_action_slot(SDL_Keycode key);
int input_action_slot_available(int slot);

/* ── Hotbar ─────────────────────────────────────────────────────────────
 *
 * The hotbar is a row of slots that each hold an inventory item. Players
 * drag items from inventory onto a hotbar slot, and press the slot's
 * bound key to use the item (sends cmd_use_inv to the server).
 *
 * Each slot tracks both the inventory position AND the item type (sprite
 * ID). When an item gets consumed, the hotbar automatically scans the
 * inventory for the next item of the same type and re-points the slot.
 * This lets you spam a key to use all potions of a kind in sequence.
 *
 * Each slot has:
 *   - an inventory index (0 = empty, >0 = item at that inventory position)
 *   - the item type that was there when assigned (for auto-refill)
 *   - a keybinding in INPUT_CAT_HOTBAR (id "hotbar.0" through "hotbar.9")
 *
 * The hotbar contents are persisted in keybinds.json alongside key overrides.
 */

#define HOTBAR_SLOTS_PER_ROW 15
#define HOTBAR_MAX_ROWS      3
#define HOTBAR_MAX_SLOTS     (HOTBAR_SLOTS_PER_ROW * HOTBAR_MAX_ROWS) /* 45 */
#define HOTBAR_DEFAULT_SLOTS HOTBAR_SLOTS_PER_ROW /* visible per row */

#define INVENTORY_EQUIP_SLOTS  30
#define SPELL_ICON_SPRITE_BASE 800
#define HOTBAR_FLASH_TICKS     3

/* what kind of thing is in a hotbar slot */
typedef enum {
	HOTBAR_EMPTY,
	HOTBAR_ITEM, /* inventory item — use via cmd_use_inv */
	HOTBAR_SPELL, /* spell/action — cast or activate via action bar */
} HotbarSlotType;

/* ── Multi-bind: per-binding overrides for cast mode and target ─────── */

#define HOTBAR_MAX_BINDS 4

typedef enum {
	HOTBAR_CAST_DEFAULT, /* use global cast_mode setting */
	HOTBAR_CAST_NORMAL,
	HOTBAR_CAST_QUICK,
	HOTBAR_CAST_INDICATOR,
} HotbarCastOverride;

typedef enum {
	HOTBAR_TGT_DEFAULT, /* use resolve_spell_action() (shift for alt) */
	HOTBAR_TGT_MAP,
	HOTBAR_TGT_CHR,
	HOTBAR_TGT_SELF,
} HotbarTargetOverride;

/* valid target flags for a given action slot (bitmask) */
#define HOTBAR_VTGT_CHR  (1 << 0)
#define HOTBAR_VTGT_MAP  (1 << 1)
#define HOTBAR_VTGT_SELF (1 << 2)

/* returns bitmask of valid targets for an action slot, 0 = self-only/instant */
DLL_EXPORT int hotbar_spell_valid_targets(int action_slot);
DLL_EXPORT int hotbar_spell_has_cast_modes(int action_slot);

typedef struct {
	SDL_Keycode key;
	Uint8 modifiers;
	HotbarCastOverride cast_override;
	HotbarTargetOverride target_override;
} HotbarBind;

typedef struct {
	HotbarSlotType type;

	/* HOTBAR_ITEM fields */
	int inv_index; /* inventory position (0 = none found yet) */
	uint32_t item_type; /* item[] sprite ID for auto-refill */

	/* HOTBAR_SPELL fields */
	int action_slot; /* ACTION_* index (0-13) */
	HotbarTargetOverride primary_target; /* target override for the primary key */

	/* additional keybindings with per-bind cast/target overrides */
	HotbarBind extra_binds[HOTBAR_MAX_BINDS];
	int extra_bind_count;

	uint32_t activated_at;
} HotbarSlot;

/* how many hotbar slots are visible per row */
DLL_EXPORT int hotbar_visible_slots(void);
DLL_EXPORT int hotbar_rows(void);
DLL_EXPORT void hotbar_set_rows(int count);
DLL_EXPORT void hotbar_set_visible_slots(int count);

/* casting modes for targeted spells (self-cast always fires immediately) */
enum {
	CAST_NORMAL, /* press key → cursor changes → click target → casts */
	CAST_QUICK, /* press key → instantly casts at cursor position */
	CAST_QUICK_INDICATOR, /* hold key → cursor changes → release key → casts */
};

DLL_EXPORT int hotbar_cast_mode(void);
DLL_EXPORT void hotbar_set_cast_mode(int mode);

/* key release handler for Quick Cast w/ Indicator mode */
void hotbar_hotkey_release(int slot);

/* cancel a Quick Cast w/ Indicator hold (right-click to cancel) */
void hotbar_cancel_held(void);

/* call on any key release — handles Quick Cast w/ Indicator */
void input_keyup(SDL_Keycode key);

/* toggle-able UI elements */
DLL_EXPORT int hotbar_show_hotkeys(void);
DLL_EXPORT void hotbar_set_show_hotkeys(int on);
DLL_EXPORT int hotbar_show_names(void);
DLL_EXPORT void hotbar_set_show_names(int on);

/* slot management */
DLL_EXPORT void hotbar_assign_item(int slot, int inventory_index);
DLL_EXPORT void hotbar_assign_item_by_type(int slot, uint32_t item_type);
DLL_EXPORT void hotbar_assign_spell(int slot, int action_slot);
DLL_EXPORT void hotbar_clear(int slot);
DLL_EXPORT void hotbar_clear_all(void);
DLL_EXPORT void hotbar_swap(int a, int b);
void hotbar_setup_defaults(void);
DLL_EXPORT const HotbarSlot *hotbar_get(int slot);

/* primary target override */
void hotbar_set_primary_target(int slot, HotbarTargetOverride tgt);

/* extra keybindings per slot */
int hotbar_add_bind(int slot, SDL_Keycode key, Uint8 mods, HotbarCastOverride cast, HotbarTargetOverride target);
int hotbar_remove_bind(int slot, int bind_index);
void hotbar_clear_binds(int slot);
int hotbar_find_extra_bind(SDL_Keycode key, Uint8 mods);
void hotbar_activate_extra(int slot, SDL_Keycode key, Uint8 mods);

/* in-place modification of extra bindings */
int hotbar_set_bind_key(int slot, int bind_index, SDL_Keycode key, Uint8 mods);
int hotbar_set_bind_cast(int slot, int bind_index, HotbarCastOverride cast);
int hotbar_set_bind_target(int slot, int bind_index, HotbarTargetOverride target);

/* returns the sprite to display in a hotbar slot (item sprite or spell icon) */
DLL_EXPORT uint32_t hotbar_slot_sprite(int slot);
DLL_EXPORT const char *hotbar_slot_name(int slot);
DLL_EXPORT void hotbar_activate(int slot);
DLL_EXPORT void hotbar_activate_with_mode(int slot, int mode);

/* call from sv_setitem when an inventory slot changes */
void hotbar_on_item_changed(int inventory_index);

/* rendering and interaction (implemented in hotbar_ui.c) */
void hotbar_init_dots(void);
void hotbar_display(void);
int hotbar_click(int slot);
int hotbar_mousedown(int slot);
void hotbar_cancel_drag(void);
int hotbar_is_dragging(void);

/* config persistence */
int input_load_config(const char *path);
int input_save_config(const char *path);
int input_migrate_binary_config(const char *path);

/* ── Keyboard movement ─────────────────────────────────────────────────── */

#define KMOVE_UP    0
#define KMOVE_DOWN  1
#define KMOVE_LEFT  2
#define KMOVE_RIGHT 3

void keyboard_move_press(int dir);
void keyboard_move_release(int dir);
void keyboard_move_tick(void);
DLL_EXPORT int keyboard_move_active(void);

/* utilities */
DLL_EXPORT Uint8 input_current_modifiers(void);
DLL_EXPORT const char *input_key_to_string(SDL_Keycode key, Uint8 modifiers);
DLL_EXPORT int input_string_to_key(const char *str, SDL_Keycode *out_key, Uint8 *out_modifiers);

#endif /* INPUT_BIND_H */
