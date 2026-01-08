/*
 * V35 Server Support Mod
 *
 * This mod adds support for v3.5 protocol servers by providing version-specific
 * configuration through the amod hook system.
 *
 * Build as amod.so/amod.dll to enable v35 support.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../amod.h"

// V35 skill indices (different from V3)
#define V35_HP          0
#define V35_ENDURANCE   1
#define V35_MANA        2
#define V35_WIS         3
#define V35_INT         4
#define V35_AGI         5
#define V35_STR         6
#define V35_ARMOR       7
#define V35_WEAPON      8
#define V35_OFFENSE     9
#define V35_DEFENSE     10
#define V35_LIGHT       11
#define V35_SPEED       12
#define V35_DAGGER      13
#define V35_HAND        14
#define V35_STAFF       15
#define V35_SWORD       16
#define V35_TWOHAND     17
#define V35_ATTACK      18
#define V35_PARRY       19
#define V35_WARCRY      20
#define V35_TACTICS     21
#define V35_SURROUND    22
#define V35_SPEEDSKILL  23
#define V35_BARTER      24
#define V35_PERCEPT     25
#define V35_STEALTH     26
#define V35_BLESS       27
#define V35_HEAL        28
#define V35_FREEZE      29
#define V35_MAGICSHIELD 30
#define V35_FLASH       31
#define V35_FIRE        32
#define V35_REGENERATE  33
#define V35_MEDITATE    34
#define V35_IMMUNITY    35
#define V35_DEMON       36
#define V35_DURATION    37
#define V35_RAGE        38
#define V35_COLD        39
#define V35_PROFESSION  40

#define V35_PROFBASE      50
#define V35_INVENTORYSIZE 70
#define V35_CONTAINERSIZE 130
#define P35_MAX           10

// State
static int is_v35 = 0;

// Forward declarations
#include "v35_skills.c"
#include "v35_keytab.c"
#include "v35_actions.c"
#include "v35_teleport.c"

DLL_EXPORT char *amod_version(void)
{
	return "V35 Support Mod 1.0";
}

DLL_EXPORT void amod_init(void)
{
	note("V35 Support Mod loaded - waiting for server version detection");
}

DLL_EXPORT void amod_gamestart(void)
{
	if (is_v35) {
		note("V35 mode active - using v3.5 protocol");
	}
}

DLL_EXPORT void amod_configure_version(int version)
{
	if (version == 35) {
		is_v35 = 1;
		note("Detected v35 server - configuring v35 support");

		// Configure dynamic sizes
		set_inventory_size(V35_INVENTORYSIZE);
		set_container_size(V35_CONTAINERSIZE);
		set_profbase(V35_PROFBASE);
		set_v_max(V35_PROFBASE + P35_MAX);
	} else {
		is_v35 = 0;
	}
}

DLL_EXPORT int amod_get_packet_length(unsigned char cmd)
{
	if (!is_v35) {
		return 0; // use default v3 lengths
	}

	// V35 has different packet lengths for some commands
	switch (cmd) {
	case 52: // SV_TELEPORT
		return 9; // v35: 64 destinations = 8 bytes + 1 cmd
	case 55: // SV_PROF
		return P35_MAX + 1; // v35: 10 professions + 1 cmd
	}
	return 0; // use default
}

DLL_EXPORT struct skill *amod_get_skill_table(int *count)
{
	if (!is_v35) {
		return NULL; // use default
	}
	*count = V35_PROFBASE + P35_MAX;
	return v35_game_skill;
}

DLL_EXPORT char **amod_get_skill_descriptions(int *count)
{
	if (!is_v35) {
		return NULL; // use default
	}
	*count = V35_PROFBASE + P35_MAX;
	return v35_game_skilldesc;
}

DLL_EXPORT struct keytab *amod_get_keytab(int *count)
{
	if (!is_v35) {
		return NULL; // use default
	}
	*count = sizeof(v35_keytab) / sizeof(v35_keytab[0]);
	return v35_keytab;
}

DLL_EXPORT int amod_get_teleport_mirror_offset(void)
{
	if (!is_v35) {
		return 0; // use default (100)
	}
	return V35_MIRROR_OFFSET; // v35 uses 200 as mirror offset
}

DLL_EXPORT int *amod_get_teleport_data(int *count)
{
	if (!is_v35) {
		return NULL; // use default
	}
	*count = 64; // v35 supports up to 64 teleport destinations
	return v35_tele;
}

DLL_EXPORT int *amod_get_action_skills(void)
{
	if (!is_v35) {
		return NULL; // use default
	}
	return v35_action_skill;
}

DLL_EXPORT char **amod_get_action_texts(void)
{
	if (!is_v35) {
		return NULL; // use default
	}
	return v35_action_text;
}

DLL_EXPORT char **amod_get_action_descs(void)
{
	if (!is_v35) {
		return NULL; // use default
	}
	return v35_action_desc;
}

DLL_EXPORT char (*amod_get_action_row(void))[14]
{
	if (!is_v35) {
		return NULL; // use default
	}
	return v35_action_row;
}

// V35 display hooks

// Display skill line - handles V35-specific skills like Offense/Defense
DLL_EXPORT int amod_display_skill_line(int v, int base, int curr, int cn, char *buf)
{
	(void)cn; // unused in v35 display

	if (!is_v35) {
		return 0; // not handled
	}

	// V35: Offense and Defense include rage bonus in curr
	if (v == V35_OFFENSE) {
		int rage_bonus = curr - base;
		if (rage_bonus > 0) {
			sprintf(buf, "%d (+%d)", base, rage_bonus);
		} else {
			sprintf(buf, "%d", base);
		}
		return 1;
	}
	if (v == V35_DEFENSE) {
		int rage_bonus = curr - base;
		if (rage_bonus > 0) {
			sprintf(buf, "%d (+%d)", base, rage_bonus);
		} else {
			sprintf(buf, "%d", base);
		}
		return 1;
	}

	return 0; // use default formatting
}

// Display rage bar - V35 has a rage mechanic
DLL_EXPORT int amod_display_rage(int rage, int max_rage, char *hover_text)
{
	if (!is_v35) {
		return 0; // not handled
	}

	// Format hover text for rage bar
	if (hover_text) {
		sprintf(hover_text, "Rage: %d/%d", rage, max_rage);
	}

	return 1; // handled - display the rage bar
}

// Get warcry cost - V35 has different mana costs
DLL_EXPORT int amod_get_warcry_cost(int *cost)
{
	if (!is_v35) {
		return 0; // use default
	}

	// V35 warcry costs 5 endurance per skill level
	*cost = 5;
	return 1;
}

// Client command handler for testing
DLL_EXPORT int amod_client_cmd(const char *buf)
{
	if (!strncmp(buf, "#v35status", 10)) {
		if (is_v35) {
			addline("V35 mode is ACTIVE");
		} else {
			addline("V35 mode is INACTIVE (v3 mode)");
		}
		return 1;
	}
	return 0;
}

// V35 Random Dungeon shrine display
// Adds the extra Welding row and removes Kindness
DLL_EXPORT int do_display_random(void)
{
	if (!is_v35) {
		return _do_display_random();
	}

	int y = doty(DOT_HLP) + 15, x, n;
	unsigned int idx, bit, m;
	static short indec[10] = {0, 11, 24, 38, 43, 57, 64, 76, 83, 96};
	static short bribes[10] = {0, 15, 22, 34, 48, 54, 67, 78, 86, 93};
	static short welding[10] = {0, 18, 27, 32, 46, 52, 62, 72, 81, 98};
	static short welding2[10] = {0, 12, 25, 35, 44, 56, 65, 77, 89, 95}; // V35-specific
	static short edge[10] = {0, 13, 26, 36, 42, 59, 66, 74, 88, 91};
	static short jobless[10] = {0, 20, 45, 61, 82, 97};
	static short security[10] = {0, 10, 29, 41, 58, 69, 75, 85, 94};

	render_text(dotx(DOT_HLP) + (10 + 204) / 2, y, whitecolor, RENDER_ALIGN_CENTER, "Random Dungeon");
	y += 24;

	render_text_fmt(dotx(DOT_HLP) + 10, y, graycolor, 0, "Continuity: %d", shrine.continuity);
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Indecisiveness: ");
	for (n = 1; n < 10; n++) {
		idx = (unsigned int)n / 32U;
		bit = 1U << ((unsigned int)n & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (indec[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", indec[n]);
			}
		}
	}
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Bribes: ");
	for (n = 1; n < 10; n++) {
		m = (unsigned int)n + 10U;
		idx = m / 32U;
		bit = 1U << (m & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (bribes[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", bribes[n]);
			}
		}
	}
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Welding: ");
	for (n = 1; n < 10; n++) {
		m = (unsigned int)n + 20U;
		idx = m / 32U;
		bit = 1U << (m & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (welding[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", welding[n]);
			}
		}
	}
	y += 12;

	// V35: Additional Welding row (indices 72+)
	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Welding: ");
	for (n = 1; n < 10; n++) {
		m = (unsigned int)n + 72U;
		idx = m / 32U;
		bit = 1 << (m & 31);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (welding2[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", welding2[n]);
			}
		}
	}
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "LOE: ");
	for (n = 1; n < 10; n++) {
		m = (unsigned int)n + 30U;
		idx = m / 32U;
		bit = 1U << (m & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (edge[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", edge[n]);
			}
		}
	}
	y += 12;

	// V35: No Kindness row (only in v30)

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Security: ");
	for (n = 1; n < 9; n++) {
		m = (unsigned int)n + 53U;
		idx = m / 32U;
		bit = 1U << (m & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (security[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", security[n]);
			}
		}
	}
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Jobless: ");
	for (n = 1; n < 6; n++) {
		m = (unsigned int)n + 63U;
		idx = m / 32U;
		bit = 1U << (m & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (jobless[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", jobless[n]);
			}
		}
	}
	y += 12;

	return y;
}

// Character sprite translation for V35-specific NPCs
// Returns 1 if sprite was handled, 0 to use default
DLL_EXPORT int trans_charno(int csprite, int *pscale, int *pcr, int *pcg, int *pcb, int *plight, int *psat, int *pc1,
    int *pc2, int *pc3, int *pshine, int attick)
{
	(void)pscale;
	(void)plight;
	(void)psat;
	(void)pshine;
	(void)attick;

	if (!is_v35) {
		return _trans_charno(csprite, pscale, pcr, pcg, pcb, plight, psat, pc1, pc2, pc3, pshine, attick);
	}

	// V35-specific character sprites
	switch (csprite) {
	case 569: // bridge guard 1
		*pcr = *pcg = *pcb = 66;
		*pc1 = IRGB(16, 31, 16);
		*pc2 = IRGB(6, 20, 6);
		*pc3 = IRGB(28, 16, 16);
		return 66;
	case 570: // bridge guard 2
		*pcr = *pcg = *pcb = 81;
		*pc1 = IRGB(16, 31, 16);
		*pc2 = IRGB(6, 20, 6);
		*pc3 = IRGB(28, 16, 16);
		return 81;
	case 571: // student
		*pcr = *pcg = *pcb = 46;
		*pc1 = IRGB(31, 16, 16);
		*pc2 = IRGB(20, 6, 6);
		*pc3 = IRGB(28, 16, 16);
		return 46;
	case 572: // gladiator
		*pcr = *pcg = *pcb = 46;
		*pc1 = IRGB(31, 20, 20);
		*pc2 = IRGB(16, 6, 6);
		*pc3 = IRGB(22, 16, 16);
		return 46;
	case 573: // teacher 1
		*pcr = *pcg = *pcb = 111;
		*pc1 = IRGB(16, 8, 8);
		*pc2 = IRGB(14, 6, 6);
		*pc3 = IRGB(24, 22, 8);
		return 111;
	case 574: // teacher 2
		*pcr = *pcg = *pcb = 101;
		*pc1 = IRGB(16, 8, 8);
		*pc2 = IRGB(14, 6, 6);
		*pc3 = IRGB(8, 8, 8);
		return 101;
	case 575: // teacher 3
		*pcr = *pcg = *pcb = 102;
		*pc1 = IRGB(16, 8, 8);
		*pc2 = IRGB(14, 6, 6);
		*pc3 = IRGB(12, 8, 8);
		return 102;
	case 576: // teacher 4
		*pcr = *pcg = *pcb = 110;
		*pc1 = IRGB(8, 8, 16);
		*pc2 = IRGB(6, 6, 14);
		*pc3 = IRGB(24, 22, 8);
		return 110;
	case 577: // misc NPC
		*pcr = *pcg = *pcb = 89;
		*pc1 = IRGB(31, 20, 20);
		*pc2 = IRGB(16, 6, 6);
		*pc3 = IRGB(22, 16, 16);
		return 89;
	}

	// Fall back to default handler for other sprites
	return _trans_charno(csprite, pscale, pcr, pcg, pcb, plight, psat, pc1, pc2, pc3, pshine, attick);
}

// ============================================================================
// V35 Otext Display System
// ============================================================================

#define MAXOTEXT 10

struct v35_otext {
	char *text;
	int time;
	int type;
};

static struct v35_otext otext[MAXOTEXT];

// Process text messages with '#0' prefix (otext)
DLL_EXPORT int amod_process_text(const char *line)
{
	if (!is_v35) {
		return 0; // not handled
	}

	// '#0' prefix indicates otext
	if (line[0] == '#' && line[1] == '0') {
		// Free old text at end of queue
		if (otext[MAXOTEXT - 1].text) {
			free(otext[MAXOTEXT - 1].text);
		}
		// Shift all entries down
		memmove(otext + 1, otext, sizeof(otext) - sizeof(otext[0]));
		// Add new text at front
		otext[0].text = strdup(line + 3); // skip '#0X' where X is type
		otext[0].time = tick;
		otext[0].type = line[2] - '0';
		return 1; // handled
	}

	return 0; // not handled
}

// Otext display
DLL_EXPORT void amod_display_game_extra(void)
{
	if (!is_v35) {
		return;
	}

	// Display otext messages
	int n, cnt;
	unsigned short col;

	for (n = cnt = 0; n < MAXOTEXT; n++) {
		if (!otext[n].text) {
			continue;
		}
		if (otext[n].type < 3 && tick - otext[n].time > TICKS * 5) {
			continue;
		}
		if (tick - otext[n].time > TICKS * 65) {
			continue;
		}
		if (otext[n].type > 1) {
			if (n == 0) {
				col = redcolor;
			} else {
				col = darkredcolor;
			}
		} else {
			if (n == 0) {
				col = greencolor;
			} else {
				col = darkgreencolor;
			}
		}
		render_text(
		    400, 420 - cnt * 12, col, RENDER_TEXT_LARGE | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, otext[n].text);
		cnt++;
	}

	// Note: Heal animation requires integration into the map rendering loop
	// and can't be easily done from this hook. The default heal sprite (50114)
	// will still display from the base client's display_game_spells().
}

// Update hover texts for V35-specific displays
DLL_EXPORT void amod_update_hover_texts(void)
{
	if (!is_v35) {
		return;
	}

	// Update hover_heal_text for any active heal effects
	int has_heal = 0;
	int heal_time = 0;

	for (int nr = 0; nr < MAXEF; nr++) {
		if (ueffect[nr] == 10) { // heal effect
			has_heal = 1;
			// Track how long heal has been active
			int elapsed = tick - (int)ceffect[nr].heal.start;
			if (elapsed > heal_time) {
				heal_time = elapsed;
			}
		}
	}

	if (has_heal) {
		snprintf(hover_heal_text, sizeof(hover_heal_text), "Healing active");
	} else {
		hover_heal_text[0] = '\0';
	}
}
