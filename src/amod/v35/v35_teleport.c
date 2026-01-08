/*
 * V35 Teleport Data
 *
 * V35 uses the same teleport map coordinates as V3.
 * The main difference is the packet format:
 * - V3: SV_TELEPORT is 13 bytes (96 teleport bits = 12 bytes + 1 cmd)
 * - V35: SV_TELEPORT is 9 bytes (64 teleport bits = 8 bytes + 1 cmd)
 *
 * V35 also uses a different mirror offset (200 vs 100 in V3).
 */

// V35 teleport coordinates - same positions as V3 for the first 26 destinations
// V35 servers may have fewer destinations due to smaller packet size
static int v35_tele[64 * 2] = {
    133,
    229, // 0  Cameron
    -1,
    -1, // 1
    143,
    206, // 2  Aston
    370,
    191, // 3  Tribe of the Isara
    370,
    179, // 4  Tribe of the Cerasa
    370,
    167, // 5  Cerasa Maze
    370,
    155, // 6  Cerasa Tunnels
    370,
    143, // 7  Zalina Entrance
    370,
    131, // 8  Tribe of the Zalina
    130,
    123, // 9  Teufelheim
    -1,
    -1, // 10
    -1,
    -1, // 11
    458,
    108, // 12 Ice 8
    458,
    96, // 13 Ice 7
    458,
    84, // 14 Ice 6
    458,
    72, // 15 Ice 5
    458,
    60, // 16 Ice 4
    225,
    123, // 17 Nomad Plains
    -1,
    -1, // 18
    -1,
    -1, // 19
    162,
    180, // 20 Forest
    164,
    167, // 21 Exkordon
    194,
    146, // 22 Brannington
    174,
    115, // 23 Grimroot
    139,
    149, // 24 Caligar
    205,
    132, // 25 Arkhata
    0,
    0, // End marker
    // Remaining slots are zero-initialized
};

// V35 mirror offset for teleport selection
// In V3, mirror selection returns n + 101
// In V35, it uses n + 200 (200 offset instead of 100)
#define V35_MIRROR_OFFSET 200
