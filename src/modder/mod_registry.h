/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Mod discovery.
 *
 * One folder per mod under <userdir>/mods/, each holding a mod.json plus any
 * mix of a native library and Lua scripts. This layer owns everything that is
 * not symbol binding - enumeration, manifest parsing, user state (enabled /
 * load order) - so the native loader (modder.c) and the Lua loader
 * (scripting/lua_core.c) work from one list instead of scanning separately.
 */

#ifndef MOD_REGISTRY_H
#define MOD_REGISTRY_H

#include "astonia.h"

#define MOD_MAX         64 /* ceiling on discovered mods */
#define MOD_ID_LEN      64
#define MOD_NAME_LEN    64
#define MOD_VERSION_LEN 32

/* Defaults for a mod that mods.json does not mention. */
#define MOD_DEFAULT_ORDER 100

struct mod_desc {
	char id[MOD_ID_LEN]; /* manifest "id", else the folder name */
	char name[MOD_NAME_LEN]; /* display name, defaults to id */
	char version[MOD_VERSION_LEN]; /* defaults to "unknown" */
	char dir[MAX_PATH]; /* mod folder, trailing separator */
	char libpath[MAX_PATH]; /* native library, "" if this mod has none */
	int enabled; /* from mods.json, default 1 */
	int order; /* from mods.json, default MOD_DEFAULT_ORDER */
	int has_lua; /* at least one *.lua in the folder */
};

/* Enumerate <userdir>/mods/, parse manifests, apply mods.json and sort by
 * (order, id). Safe to call again - it discards and rebuilds the list.
 * Returns the number of mods found (including disabled ones). */
int mod_registry_scan(void);

/* Same, against an explicit root (trailing separator required). Used by the
 * unit test, which must not drag the whole client in just to get a path. */
int mod_registry_scan_root(const char *root);

int mod_registry_count(void);
const struct mod_desc *mod_registry_get(int idx);

/* Flip a mod's enabled state and persist mods.json. Takes effect at the next
 * launch: loaded libraries are never unloaded. Returns 1 on success. */
int mod_registry_set_enabled(const char *id, int enabled);

#endif
