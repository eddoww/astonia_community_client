/*
 * V35 Skill Definitions
 *
 * V35 has different skill structure than V3:
 * - Different skill indices (no Pulse, no Armor Skill)
 * - Added Offense/Defense skills
 * - Different profession count (10 vs 20)
 */

// V35 skill table
static struct skill v35_game_skill[V_MAX] = {
    // Powers
    {"Hitpoints", -1, -1, -1, 3, 10}, // 0
    {"Endurance", -1, -1, -1, 3, 10}, // 1
    {"Mana", -1, -1, -1, 3, 10}, // 2

    // Attributes
    {"Wisdom", -1, -1, -1, 2, 10}, // 3
    {"Intuition", -1, -1, -1, 2, 10}, // 4
    {"Agility", -1, -1, -1, 2, 10}, // 5
    {"Strength", -1, -1, -1, 2, 10}, // 6

    // Values
    {"Armor", -1, -1, -1, 0, 0}, // 7
    {"Weapon", -1, -1, -1, 0, 0}, // 8
    {"Offense", -1, -1, -1, 0, 0}, // 9  - V35 specific
    {"Defense", -1, -1, -1, 0, 0}, // 10 - V35 specific
    {"Light", -1, -1, -1, 0, 0}, // 11
    {"Speed", V35_AGI, V35_AGI, V35_STR, 0, 0}, // 12

    // Primary Fighting Skills
    {"Dagger", V35_INT, V35_AGI, V35_STR, 1, 1}, // 13
    {"Hand to Hand", V35_AGI, V35_STR, V35_STR, 1, 1}, // 14
    {"Staff", V35_INT, V35_AGI, V35_STR, 1, 1}, // 15
    {"Sword", V35_INT, V35_AGI, V35_STR, 1, 1}, // 16
    {"Two-Handed", V35_AGI, V35_STR, V35_STR, 1, 1}, // 17

    // Secondary Fighting Skills
    {"Attack", V35_INT, V35_AGI, V35_STR, 1, 1}, // 18
    {"Parry", V35_INT, V35_AGI, V35_STR, 1, 1}, // 19
    {"Warcry", V35_INT, V35_AGI, V35_STR, 1, 1}, // 20
    {"Tactics", V35_INT, V35_AGI, V35_STR, 1, 1}, // 21
    {"Surround Hit", V35_INT, V35_AGI, V35_STR, 1, 1}, // 22
    {"Speed Skill", V35_INT, V35_AGI, V35_STR, 1, 1}, // 23

    // Misc Skills
    {"Bartering", V35_INT, V35_INT, V35_WIS, 1, 1}, // 24
    {"Perception", V35_INT, V35_INT, V35_WIS, 1, 1}, // 25
    {"Stealth", V35_INT, V35_AGI, V35_AGI, 1, 1}, // 26

    // Spells
    {"Bless", V35_INT, V35_INT, V35_WIS, 1, 1}, // 27
    {"Heal", V35_INT, V35_INT, V35_WIS, 1, 1}, // 28
    {"Freeze", V35_INT, V35_INT, V35_WIS, 1, 1}, // 29
    {"Magic Shield", V35_INT, V35_INT, V35_WIS, 1, 1}, // 30
    {"Lightning", V35_INT, V35_INT, V35_WIS, 1, 1}, // 31
    {"Fire", V35_INT, V35_INT, V35_WIS, 1, 1}, // 32

    {"Regenerate", V35_STR, V35_STR, V35_STR, 1, 1}, // 33
    {"Meditate", V35_WIS, V35_WIS, V35_WIS, 1, 1}, // 34
    {"Immunity", V35_INT, V35_WIS, V35_STR, 1, 1}, // 35

    {"Ancient Knowledge", V35_INT, V35_WIS, V35_STR, 1, 1}, // 36
    {"Duration", V35_INT, V35_INT, V35_WIS, 1, 1}, // 37
    {"Rage", V35_STR, V35_STR, V35_STR, 1, 1}, // 38
    {"Resist Cold", V35_INT, V35_WIS, V35_STR, 1, 1}, // 39
    {"Profession", -1, -1, -1, 0, 0}, // 40

    // Padding up to V35_PROFBASE (50)
    {"", -1, -1, -1, 0, 0}, // 41
    {"", -1, -1, -1, 0, 0}, // 42
    {"", -1, -1, -1, 0, 0}, // 43
    {"", -1, -1, -1, 0, 0}, // 44
    {"", -1, -1, -1, 0, 0}, // 45
    {"", -1, -1, -1, 0, 0}, // 46
    {"", -1, -1, -1, 0, 0}, // 47
    {"", -1, -1, -1, 0, 0}, // 48
    {"", -1, -1, -1, 0, 0}, // 49

    // Professions (V35_PROFBASE = 50)
    {"Smith", -1, -1, -1, 0, 0}, // 50
    {"Alchemist", -1, -1, -1, 0, 0}, // 51
    {"Miner", -1, -1, -1, 0, 0}, // 52
    {"Enhancer", -1, -1, -1, 0, 0}, // 53
    {"Mercenary", -1, -1, -1, 0, 0}, // 54
    {"Trader", -1, -1, -1, 0, 0}, // 55
    {"", -1, -1, -1, 0, 0}, // 56
    {"", -1, -1, -1, 0, 0}, // 57
    {"", -1, -1, -1, 0, 0}, // 58
    {"", -1, -1, -1, 0, 0}, // 59
};

// V35 skill descriptions
static char *v35_game_skilldesc[V_MAX] = {
    // 0-6: Powers and Attributes
    "Determines how much damage you can take before dying.",
    "Determines how long you can run and fight before getting exhausted.",
    "Determines how much magical energy you have available for spells.",
    "Determines your magical power and resistance.",
    "Determines your perception and magical precision.",
    "Determines your speed and evasion ability.",
    "Determines your physical power and carrying capacity.",

    // 7-12: Values
    "Your armor value, reducing incoming damage.",
    "Your weapon value, determining maximum damage.",
    "Your offensive combat strength (base + rage bonus).", // V35 specific
    "Your defensive combat strength (base + rage bonus).", // V35 specific
    "Your light radius for seeing in dark areas.",
    "How fast you move around the world.",

    // 13-17: Primary Fighting Skills
    "Skill with daggers and small blades.",
    "Unarmed combat skill.",
    "Skill with staves and poles.",
    "Skill with swords.",
    "Skill with two-handed weapons.",

    // 18-23: Secondary Fighting Skills
    "Your ability to land hits on enemies.",
    "Your ability to block incoming attacks.",
    "A powerful shout that damages and stuns nearby enemies.",
    "Your tactical combat awareness.",
    "Ability to hit multiple adjacent enemies.",
    "Increases your combat speed.",

    // 24-26: Misc Skills
    "Your ability to get better prices from merchants.",
    "Your ability to notice hidden things.",
    "Your ability to move unseen.",

    // 27-32: Spells
    "Increases attributes of yourself or an ally.",
    "Restores hitpoints over time.",
    "Slows down nearby enemies.",
    "Creates a magical shield absorbing damage.",
    "Summons a ball of lightning.",
    "Throws a ball of fire that explodes on impact.",

    // 33-40: More skills
    "Passively regenerate hitpoints over time.",
    "Passively regenerate mana over time.",
    "Resistance to negative magical effects.",
    "Ancient knowledge passed down through generations.",
    "Increases the duration of your spells.",
    "Build up rage in combat for bonus damage.",
    "Resistance to cold damage.",
    "Total points invested in professions.",
};
