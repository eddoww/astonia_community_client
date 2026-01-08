/*
 * V35 Action Bar Definitions
 *
 * V35 has different skill indices and no Pulse spell.
 * Action slot 9 (Pulse in V3) is replaced with Surround Hit.
 */

#define MAXACTIONSLOT 14

// V35 action bar skill indices
// Slot order: Attack, Fireball, LBall, Flash, Freeze, Shield, Bless, Heal, Warcry, Surround, Firering, Use, Map, Look
static int v35_action_skill[MAXACTIONSLOT] = {
    V35_PERCEPT, // 0: Attack (uses perception)
    V35_FIRE, // 1: Fireball
    V35_FLASH, // 2: Lightning Ball
    V35_FLASH, // 3: Flash
    V35_FREEZE, // 4: Freeze
    V35_MAGICSHIELD, // 5: Magic Shield
    V35_BLESS, // 6: Bless
    V35_HEAL, // 7: Heal
    V35_WARCRY, // 8: Warcry
    V35_SURROUND, // 9: Surround Hit (V35: replaces Pulse)
    V35_FIRE, // 10: Firering
    V35_PERCEPT, // 11: Take/Use/Give/Drop
    -1, // 12: Map (no skill required)
    -1 // 13: Look (no skill required)
};

// V35 action bar text labels
static char *v35_action_text[MAXACTIONSLOT] = {"Attack", "Fireball", "Lightning Ball", "Flash", "Freeze",
    "Magic Shield", "Bless", "Heal", "Warcry",
    "Surround Hit", // V35: replaces Pulse
    "Firering", "Take/Use/Give/Drop", "Map", "Look"};

// V35 action bar descriptions
static char *v35_action_desc[MAXACTIONSLOT] = {"Attacks another character using your equipped weapon, or your hands.",
    "Throws a fireball. Explodes for huge splash damage when it hits.",
    "Throws a slow moving ball of lightning. It will deal medium damage over time to enemies it passes.",
    "Summons a small ball of lightning to your side. It will deal medium damage over time to enemies near you.",
    "Slows down enemies close to you.",
    "Summons a magic shield that will protect you from damage. Collapses when used up.",
    "Increases attributes of yourself or an ally.", "Restores hitpoints over time.",
    "A powerful shout that damages and stuns nearby enemies.",
    "Deals high damage to all adjacent enemies.", // V35: Surround Hit description
    "Deals high damage to adjacent enemies.",
    ("Interact with items. Can be used to take or use an item on the ground, or to drop or give an item on your mouse "
    "cursor."),
    "Cycles between the minimap, the big map and no map.", "Look at characters or items in the world."};

// V35 action row key mappings (same as V3)
static char v35_action_row[2][MAXACTIONSLOT] = {
    // 01234567890123
    "asd  fg   h l", " qwertzuiop m"};
