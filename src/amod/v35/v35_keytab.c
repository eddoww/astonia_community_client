/*
 * V35 Keytab Definitions
 *
 * V35 has different skill indices and no Pulse spell.
 * Uses V35_FIRE instead of V_FIREBALL, etc.
 */

#include <SDL3/SDL.h>

// Target types (must match gui_private.h)
#ifndef TGT_MAP
#define TGT_MAP 1
#define TGT_ITM 2
#define TGT_CHR 3
#define TGT_SLF 4
#endif

// Command IDs (must match client.h)
#ifndef CL_BLESS
#define CL_BLESS       10
#define CL_FIREBALL    11
#define CL_HEAL        12
#define CL_MAGICSHIELD 13
#define CL_FREEZE      14
#define CL_FLASH       17
#define CL_BALL        18
#define CL_WARCRY      19
#endif

// Keytab structure (must match gui_private.h)
struct keytab {
	SDL_Keycode keycode;
	SDL_Keycode userdef;
	int vk_item, vk_char, vk_spell;
	char name[14];
	int tgt;
	int cl;
	int vk;
	Uint64 usetime;
};

// V35 keytab - no Pulse, different skill indices
static struct keytab v35_keytab[] = {
    // vk_char=1, vk_spell=0 (Character targeting mode)
    {'1', 0, 0, 1, 0, "FIREBALL", TGT_CHR, CL_FIREBALL, V35_FIRE, 0},
    {'2', 0, 0, 1, 0, "LIGHTNINGBALL", TGT_CHR, CL_BALL, V35_FLASH, 0},
    {'3', 0, 0, 1, 0, "FLASH", TGT_SLF, CL_FLASH, V35_FLASH, 0},
    {'4', 0, 0, 1, 0, "FREEZE", TGT_SLF, CL_FREEZE, V35_FREEZE, 0},
    {'5', 0, 0, 1, 0, "SHIELD", TGT_SLF, CL_MAGICSHIELD, V35_MAGICSHIELD, 0},
    {'6', 0, 0, 1, 0, "BLESS", TGT_CHR, CL_BLESS, V35_BLESS, 0},
    {'7', 0, 0, 1, 0, "HEAL", TGT_CHR, CL_HEAL, V35_HEAL, 0},
    {'8', 0, 0, 1, 0, "WARCRY", TGT_SLF, CL_WARCRY, V35_WARCRY, 0},
    {'9', 0, 0, 1, 0, "FIRERING", TGT_SLF, CL_FIREBALL, V35_FIRE, 0}, // V35: no Pulse, use firering
    {'0', 0, 0, 1, 0, "FIRERING", TGT_SLF, CL_FIREBALL, V35_FIRE, 0},

    // vk_char=1, vk_spell=1 (Spell mode + character targeting)
    {'1', 0, 0, 1, 1, "FIREBALL", TGT_CHR, CL_FIREBALL, V35_FIRE, 0},
    {'2', 0, 0, 1, 1, "LIGHTNINGBALL", TGT_CHR, CL_BALL, V35_FLASH, 0},
    {'3', 0, 0, 1, 1, "FLASH", TGT_SLF, CL_FLASH, V35_FLASH, 0},
    {'4', 0, 0, 1, 1, "FREEZE", TGT_SLF, CL_FREEZE, V35_FREEZE, 0},
    {'5', 0, 0, 1, 1, "SHIELD", TGT_SLF, CL_MAGICSHIELD, V35_MAGICSHIELD, 0},
    {'6', 0, 0, 1, 1, "BLESS", TGT_CHR, CL_BLESS, V35_BLESS, 0},
    {'7', 0, 0, 1, 1, "HEAL", TGT_CHR, CL_HEAL, V35_HEAL, 0},
    {'8', 0, 0, 1, 1, "WARCRY", TGT_SLF, CL_WARCRY, V35_WARCRY, 0},
    {'9', 0, 0, 1, 1, "FIRERING", TGT_SLF, CL_FIREBALL, V35_FIRE, 0}, // V35: no Pulse
    {'0', 0, 0, 1, 1, "FIRERING", TGT_SLF, CL_FIREBALL, V35_FIRE, 0},

    // vk_char=0, vk_spell=1 (Spell mode + map targeting)
    {'1', 0, 0, 0, 1, "FIREBALL", TGT_MAP, CL_FIREBALL, V35_FIRE, 0},
    {'2', 0, 0, 0, 1, "LIGHTNINGBALL", TGT_MAP, CL_BALL, V35_FLASH, 0},
    {'3', 0, 0, 0, 1, "FLASH", TGT_SLF, CL_FLASH, V35_FLASH, 0},
    {'4', 0, 0, 0, 1, "FREEZE", TGT_SLF, CL_FREEZE, V35_FREEZE, 0},
    {'5', 0, 0, 0, 1, "SHIELD", TGT_SLF, CL_MAGICSHIELD, V35_MAGICSHIELD, 0},
    {'6', 0, 0, 0, 1, "BLESS SELF", TGT_SLF, CL_BLESS, V35_BLESS, 0},
    {'7', 0, 0, 0, 1, "HEAL SELF", TGT_SLF, CL_HEAL, V35_HEAL, 0},
    {'8', 0, 0, 0, 1, "WARCRY", TGT_SLF, CL_WARCRY, V35_WARCRY, 0},
    {'9', 0, 0, 0, 1, "FIRERING", TGT_SLF, CL_FIREBALL, V35_FIRE, 0}, // V35: no Pulse
    {'0', 0, 0, 0, 1, "FIRERING", TGT_SLF, CL_FIREBALL, V35_FIRE, 0},
};
