/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Mod Structures Header - Data structure definitions for mod API
 *
 * This header can be used standalone or included after astonia.h.
 * It provides all structure definitions needed for mod development.
 */

#ifndef AMOD_STRUCTS_H
#define AMOD_STRUCTS_H

#include <stdint.h>
#include <string.h>

// ============================================================================
// Type Definitions (if not already defined by astonia.h)
// ============================================================================

#ifndef ASTONIA_TYPES_DEFINED
typedef uint32_t tick_t; // SDL ticks, timestamps
typedef uint16_t stat_t; // Character stats: hp, mana, rage, endurance, lifeshield
typedef uint16_t char_id_t; // Character network ID (cn)
typedef uint16_t sprite_id_t; // Sprite/texture ID
typedef size_t map_index_t; // Map tile index
#define ASTONIA_TYPES_DEFINED
#endif

// ============================================================================
// Utility Macros
// ============================================================================

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef abs
#define abs(a) ((a) < 0 ? (-(a)) : (a))
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef bzero
#define bzero(ptr, size) memset(ptr, 0, size)
#endif

// ============================================================================
// Game Constants
// ============================================================================

#define V_MAX         200
#define DIST          ((unsigned int)25)
#define MAPDX         (DIST * 2 + 1)
#define MAPDY         (DIST * 2 + 1)
#define MAXMN         (MAPDX * MAPDY)
#define INVENTORYSIZE 110
#define CONTAINERSIZE (INVENTORYSIZE)
#define MAXCHARS      2048
#define MAXEF         64
#define MAXSHRINE     256
#define MAXQUEST      100

// ============================================================================
// Render Constants
// ============================================================================

#define RENDER_ALIGN_OFFSET 0 // Default, use bzero to initialize
#define RENDER_ALIGN_CENTER 1 // Also used in render_text
#define RENDER_ALIGN_NORMAL 2

#define RENDER_TEXT_LEFT    0
#define RENDER_TEXT_CENTER  1
#define RENDER_TEXT_RIGHT   2
#define RENDER_TEXT_SHADED  4
#define RENDER_TEXT_LARGE   0
#define RENDER_TEXT_SMALL   8
#define RENDER_TEXT_FRAMED  16
#define RENDER_TEXT_BIG     32
#define RENDER_TEXT_NOCACHE 64

#define RENDERFX_NORMAL_LIGHT 15
#define RENDERFX_BRIGHT       0

// ============================================================================
// Color Macros (16-bit IRGB format)
// ============================================================================

#define IGET_R(c)     ((((unsigned short int)(c)) >> 10) & 0x1F)
#define IGET_G(c)     ((((unsigned short int)(c)) >> 5) & 0x1F)
#define IGET_B(c)     ((((unsigned short int)(c)) >> 0) & 0x1F)
#define IRGB(r, g, b) (((r) << 10) | ((g) << 5) | ((b) << 0))

// ============================================================================
// GUI Dot Indices
// ============================================================================

#define DOT_TL  0 // top left
#define DOT_BR  1 // bottom right
#define DOT_WEA 2 // worn equipment
#define DOT_INV 3 // inventory
#define DOT_CON 4 // container
#define DOT_SCL 5 // scroll bar left, uses only X
#define DOT_SCR 6 // scroll bar right, uses only X
#define DOT_SCU 7 // scroll bars up arrows at this Y
#define DOT_SCD 8 // scroll bars down arrows at this Y
#define DOT_TXT 9 // chat window
#define DOT_MTL 10 // map top left
#define DOT_MBR 11 // map bottom right
#define DOT_SKL 12 // skill list
#define DOT_GLD 13 // gold
#define DOT_JNK 14 // trashcan
#define DOT_MOD 15 // speed mode
#define DOT_MCT 16 // map center
#define DOT_TOP 17 // top left corner of equipment bar
#define DOT_BOT 18 // top left corner of bottom window
#define DOT_TX2 19 // chat window bottom right
#define DOT_SK2 20 // skill list window bottom right
#define DOT_IN1 21 // inventory top left
#define DOT_IN2 22 // inventory bottom right
#define DOT_HLP 23 // help top left
#define DOT_HL2 24 // help bottom right
#define DOT_TEL 25 // teleporter top left
#define DOT_COL 26 // color picker top left
#define DOT_LOK 27 // look at character window top left
#define DOT_BO2 28 // bottom right of bottom window
#define DOT_ACT 29 // action bar top left
#define DOT_SSP 30 // self-spell-bars top left
#define DOT_TUT 31 // tutor window top left
#define MAX_DOT 32

// ============================================================================
// Skill/Stat Indices
// ============================================================================

#define V_HP          0
#define V_ENDURANCE   1
#define V_MANA        2
#define V_WIS         3
#define V_INT         4
#define V_AGI         5
#define V_STR         6
#define V_ARMOR       7
#define V_WEAPON      8
#define V_LIGHT       9
#define V_SPEED       10
#define V_PULSE       11
#define V_DAGGER      12
#define V_HAND        13
#define V_STAFF       14
#define V_SWORD       15
#define V_TWOHAND     16
#define V_ARMORSKILL  17
#define V_ATTACK      18
#define V_PARRY       19
#define V_WARCRY      20
#define V_TACTICS     21
#define V_SURROUND    22
#define V_BODYCONTROL 23
#define V_SPEEDSKILL  24
#define V_BARTER      25
#define V_PERCEPT     26
#define V_STEALTH     27
#define V_BLESS       28
#define V_HEAL        29
#define V_FREEZE      30
#define V_MAGICSHIELD 31
#define V_FLASH       32
#define V_FIREBALL    33
#define V_REGENERATE  35
#define V_MEDITATE    36
#define V_IMMUNITY    37
#define V_DEMON       38
#define V_DURATION    39
#define V_RAGE        40
#define V_COLD        41
#define V_PROFESSION  42

// ============================================================================
// Mouse Event Constants
// ============================================================================

#define SDL_MOUM_LUP   1
#define SDL_MOUM_LDOWN 2
#define SDL_MOUM_RUP   3
#define SDL_MOUM_RDOWN 4
#define SDL_MOUM_MUP   5
#define SDL_MOUM_MDOWN 6
#define SDL_MOUM_WHEEL 7

// ============================================================================
// Map Flags
// ============================================================================

#define CMF_LIGHT      (1 + 2 + 4 + 8)
#define CMF_VISIBLE    16
#define CMF_TAKE       32
#define CMF_USE        64
#define CMF_INFRA      128
#define CMF_UNDERWATER 256

#define MMF_SIGHTBLOCK (1 << 1)
#define MMF_DOOR       (1 << 2)
#define MMF_CUT        (1 << 3)

// ============================================================================
// Server Message Constants (for amod_process/amod_prefetch)
// ============================================================================

#define SV_MOD1 58
#define SV_MOD2 59
#define SV_MOD3 60
#define SV_MOD4 61
#define SV_MOD5 62

// ============================================================================
// Quest Flags
// ============================================================================

#define QLF_REPEATABLE (1u << 0)
#define QLF_XREPEAT    (1u << 1)

// ============================================================================
// Screen Resolution
// ============================================================================

#define XRES 800
#define YRES (__yres)

// ============================================================================
// Structures - Rendering
// ============================================================================

struct ddfx {
	unsigned int sprite; // Primary sprite number

	signed char sink;
	unsigned char scale; // Scale in percent
	char cr, cg, cb; // Color balancing
	char clight, sat; // Lightness, saturation
	unsigned short c1, c2, c3, shine; // Color replacer

	char light; // 0=bright(RENDERFX_BRIGHT) 1=almost black; 15=normal (RENDERFX_NORMAL_LIGHT)
	char freeze; // Freeze frame index

	char ml, ll, rl, ul, dl;

	char align; // RENDER_ALIGN_OFFSET, RENDER_ALIGN_CENTER, RENDER_ALIGN_NORMAL
	short int clipsx, clipex; // Additional x-clipping
	short int clipsy, clipey; // Additional y-clipping

	unsigned char alpha;
};

typedef struct ddfx RenderFX;

struct complex_sprite {
	unsigned int sprite;
	unsigned short c1, c2, c3, shine;
	unsigned char cr, cg, cb;
	unsigned char light, sat;
	unsigned char scale;
};

// ============================================================================
// Structures - Map
// ============================================================================

struct map {
	// From map & item
	unsigned short int gsprite; // Background sprite
	unsigned short int gsprite2; // Background sprite 2
	unsigned short int fsprite; // Foreground sprite
	unsigned short int fsprite2; // Foreground sprite 2

	unsigned int isprite; // Item sprite
	unsigned short ic1, ic2, ic3;

	unsigned int flags; // See CMF_*

	// Character
	unsigned int csprite; // Character base sprite
	unsigned int cn; // Character number (for commands)
	unsigned char cflags; // Character flags
	unsigned char action; // Character action
	unsigned char duration;
	unsigned char step;
	unsigned char dir; // Direction the character is facing
	unsigned char health; // Character health (in percent)
	unsigned char mana;
	unsigned char shield;

	// Effects
	unsigned int ef[4];

	unsigned char sink; // Sink characters on this field
	int value; // Testing purposes only
	int mmf; // More flags (MMF_*)
	char rlight; // Real client light: 0=invisible, 1=dark, 14=normal

	struct complex_sprite rc; // Character sprite
	struct complex_sprite ri; // Item sprite
	struct complex_sprite rf; // Foreground sprite
	struct complex_sprite rf2;
	struct complex_sprite rg; // Ground/background sprite
	struct complex_sprite rg2;

	char xadd; // X position adjustment for character sprite
	char yadd; // Y position adjustment for character sprite
};

// ============================================================================
// Structures - Skills
// ============================================================================

struct skill {
	char name[80];
	int base1, base2, base3;
	int cost; // 0=not raisable, 1=skill, 2=attribute, 3=power
	int start; // Start value, points up to this value are free
};

struct skltab {
	int v; // Negative v-values indicate special display
	int button; // Show button
	char name[80];
	int base;
	int curr;
	int raisecost;
	int barsize; // Positive=blue, negative=red
};

typedef struct skltab SKLTAB;

// ============================================================================
// Structures - Players
// ============================================================================

struct player {
	char name[80];
	sprite_id_t csprite; // Character sprite (uint16_t)
	stat_t level; // Player level (uint16_t)
	unsigned short c1, c2, c3;
	unsigned char clan;
	unsigned char pk_status;
};

// ============================================================================
// Structures - Effects
// ============================================================================

struct cef_generic {
	unsigned int nr;
	int type;
};

struct cef_shield {
	int nr;
	int type;
	char_id_t cn;
	uint32_t start;
};

struct cef_strike {
	int nr;
	int type;
	char_id_t cn;
	int x, y; // Target
};

struct cef_ball {
	int nr;
	int type;
	uint32_t start;
	int frx, fry; // High precision coords
	int tox, toy;
};

struct cef_fireball {
	int nr;
	int type;
	uint32_t start;
	int frx, fry;
	int tox, toy;
};

struct cef_edemonball {
	int nr;
	int type;
	uint32_t start;
	int base;
	int frx, fry;
	int tox, toy;
};

struct cef_flash {
	int nr;
	int type;
	char_id_t cn;
};

struct cef_explode {
	int nr;
	int type;
	uint32_t start;
	unsigned int base;
};

struct cef_warcry {
	int nr;
	int type;
	char_id_t cn;
	int stop;
};

struct cef_bless {
	int nr;
	int type;
	char_id_t cn;
	uint32_t start;
	uint32_t stop;
	int strength;
};

struct cef_heal {
	int nr;
	int type;
	char_id_t cn;
	uint32_t start;
};

struct cef_freeze {
	int nr;
	int type;
	char_id_t cn;
	uint32_t start;
	uint32_t stop;
};

struct cef_burn {
	int nr;
	int type;
	char_id_t cn;
	int stop;
};

struct cef_mist {
	int nr;
	int type;
	uint32_t start;
};

struct cef_pulse {
	int nr;
	int type;
	uint32_t start;
};

struct cef_pulseback {
	int nr;
	int type;
	char_id_t cn;
	int x, y;
};

struct cef_potion {
	int nr;
	int type;
	char_id_t cn;
	uint32_t start;
	uint32_t stop;
	int strength;
};

struct cef_earthrain {
	int nr;
	int type;
	int strength;
};

struct cef_earthmud {
	int nr;
	int type;
};

struct cef_curse {
	int nr;
	int type;
	char_id_t cn;
	uint32_t start;
	uint32_t stop;
	int strength;
};

struct cef_cap {
	int nr;
	int type;
	char_id_t cn;
};

struct cef_lag {
	int nr;
	int type;
	char_id_t cn;
};

struct cef_firering {
	int nr;
	int type;
	char_id_t cn;
	uint32_t start;
};

struct cef_bubble {
	int nr;
	int type;
	int yoff;
};

union ceffect {
	struct cef_generic generic;
	struct cef_shield shield;
	struct cef_strike strike;
	struct cef_ball ball;
	struct cef_fireball fireball;
	struct cef_flash flash;
	struct cef_explode explode;
	struct cef_warcry warcry;
	struct cef_bless bless;
	struct cef_heal heal;
	struct cef_freeze freeze;
	struct cef_burn burn;
	struct cef_mist mist;
	struct cef_potion potion;
	struct cef_earthrain earthrain;
	struct cef_earthmud earthmud;
	struct cef_edemonball edemonball;
	struct cef_curse curse;
	struct cef_cap cap;
	struct cef_lag lag;
	struct cef_pulse pulse;
	struct cef_pulseback pulseback;
	struct cef_firering firering;
	struct cef_bubble bubble;
};

// ============================================================================
// Structures - Quests
// ============================================================================

struct questlog {
	char *name;
	int minlevel, maxlevel;
	char *giver;
	char *area;
	int exp;
	unsigned int flags;
};

struct quest {
	unsigned char done : 6;
	unsigned char flags : 2;
};

struct shrine_ppd {
	unsigned int used[MAXSHRINE / 32];
	unsigned char continuity;
};

#endif // AMOD_STRUCTS_H
