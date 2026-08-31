/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * GUI panel system
 *
 * The classic bottom window is split into independent panels (skills, chat,
 * inventory, gold, speed, buffs, hotbar). Each panel can be hidden and moved;
 * layout is stored as a per-panel offset from the default position so it
 * survives init_dots() re-runs (resolution / option changes) and persists in
 * the keybind config. A master overlay toggle hides the whole GUI chrome for
 * an unobstructed view of the game world; interaction windows (teleporter,
 * context menu, help, ...) stay usable.
 */

#ifndef PANELS_H
#define PANELS_H

#include "../dll.h"

enum {
	PANEL_SKILLS, /* skill list / open container + left scrollbar */
	PANEL_CHAT, /* chat text + input line                     */
	PANEL_INVENTORY, /* inventory grid + right scrollbar           */
	PANEL_GOLD, /* gold purse + trashcan                      */
	PANEL_SPEED, /* stealth/normal/fast walk mode              */
	PANEL_BUFFS, /* self-spell bars + rage meter               */
	PANEL_HOTBAR, /* hotbar rows + spellbook chevron            */
	PANEL_EQUIPMENT, /* worn-equipment slots + gear lock           */
	MAX_PANEL
};

/* master GUI overlay toggle - session only, always starts visible */
DLL_EXPORT extern int gui_overlay_visible;

/* raw per-panel visibility toggle */
DLL_EXPORT int panel_visible(int p);
DLL_EXPORT void panel_set_visible(int p, int on);
DLL_EXPORT void panel_toggle(int p);

/* effective visibility: overlay + toggle + auto-show (an open container
 * forces the skills panel, an active chat line forces the chat panel) */
DLL_EXPORT int panel_shown(int p);

/* persistent drag offset from the default layout position */
DLL_EXPORT int panel_dx(int p);
DLL_EXPORT int panel_dy(int p);

const char *panel_id(int p); /* config key, e.g. "skills" */
const char *panel_name(int p); /* display name, e.g. "Skills Panel" */

/* shift the panel's dots/buttons by the stored offsets; call at the very
 * end of init_dots() (after every set_dot/set_but) */
void panels_apply_offsets(void);

/* drag-handle mouse capture: apply mousedx/mousedy to the panel that owns
 * the captured drag button; call panels_drag_finished() on button release
 * to persist the moved layout */
void panels_drag(int p);
void panels_drag_finished(void);

/* which panel owns button b, or -1 (drag handles included) */
int panel_owns_button(int b);

/* subtle drag-handle indicator near the mouse; call late in display() */
void panels_display_handles(void);

/* forget all offsets and visibility toggles (does not touch the
 * fullscreen-world setting); caller re-runs init_dots() + saves */
void panels_reset_layout(void);

/* fullscreen world view: map spans the whole canvas, GUI overlays it */
DLL_EXPORT int panels_fullscreen_world(void);
void panels_set_fullscreen_world(int on);

/* persistence hooks for the keybind config (cJSON tree) */
struct cJSON;
void panels_save_json(struct cJSON *root);
void panels_load_json(const struct cJSON *root);

#endif
