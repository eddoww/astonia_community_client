/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Mod discovery - see mod_registry.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include "astonia.h"
#include "modder/mod_registry.h"
#include "client/client_private.h"
#include "lib/cjson/cJSON.h"

#ifdef _WIN32
#define MOD_LIB_EXT "dll"
#elif defined(SDL_PLATFORM_APPLE)
#define MOD_LIB_EXT "dylib"
#else
#define MOD_LIB_EXT "so"
#endif

#define MOD_MANIFEST "mod.json"
#define MOD_STATE    "mods.json"

static struct mod_desc mods[MOD_MAX];
static int mod_count;

/* Root the last scan ran against, kept so set_enabled knows where mods.json
 * lives without asking the client again (and so unit tests can drive it). */
static char mod_root[MAX_PATH];

int mod_registry_count(void)
{
	return mod_count;
}

const struct mod_desc *mod_registry_get(int idx)
{
	if (idx < 0 || idx >= mod_count) {
		return NULL;
	}
	return &mods[idx];
}

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

/* Read a whole file. Self-contained (no xmalloc/MEM_*) so the registry can be
 * unit tested without dragging in the client's memory tracking. */
static char *read_file(const char *path)
{
	FILE *fp;
	long len;
	char *buf;

	if (!(fp = fopen(path, "rb"))) {
		return NULL;
	}
	if (fseek(fp, 0, SEEK_END) != 0 || (len = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return NULL;
	}
	/* a manifest this large is not a manifest */
	if (len > 1024 * 1024) {
		fclose(fp);
		return NULL;
	}
	if (!(buf = malloc((size_t)len + 1))) {
		fclose(fp);
		return NULL;
	}
	if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
		free(buf);
		fclose(fp);
		return NULL;
	}
	buf[len] = 0;
	fclose(fp);
	return buf;
}

/* Copy a manifest string field into a fixed buffer, leaving the buffer alone
 * when the key is missing or not a (non-empty) string. */
static void json_str(cJSON *obj, const char *key, char *dst, size_t dstsize)
{
	cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);

	if (cJSON_IsString(item) && item->valuestring && item->valuestring[0]) {
		snprintf(dst, dstsize, "%s", item->valuestring);
	}
}

/* A mod id becomes a directory name and a mods.json key, so keep it to
 * something unsurprising. Anything else falls back to the folder name. */
static int valid_id(const char *id)
{
	int i;

	if (!id || !id[0]) {
		return 0;
	}
	for (i = 0; id[i]; i++) {
		char c = id[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		        c == '.')) {
			return 0;
		}
	}
	return 1;
}

static int has_file(const char *dir, const char *name)
{
	char path[MAX_PATH];
	SDL_PathInfo info;

	snprintf(path, sizeof(path), "%s%s", dir, name);
	return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

/* Does the folder hold at least one *.lua? */
static int scan_lua(const char *dir)
{
	int count = 0;
	char **list = SDL_GlobDirectory(dir, "*.lua", SDL_GLOB_CASEINSENSITIVE, &count);

	if (!list) {
		return 0;
	}
	SDL_free(list);
	return count > 0;
}

/* Locate the mod's native library. An explicit manifest "entry" wins (the
 * extension is ours, so a mod ships one name for all three platforms);
 * otherwise the folder must hold exactly one library for this platform.
 * Ambiguity is refused rather than guessed at - see the warn below. */
static void scan_library(struct mod_desc *m, const char *entry)
{
	char pattern[64];
	char path[MAX_PATH];
	SDL_PathInfo info;
	char **list;
	int count = 0;

	m->libpath[0] = 0;

	if (entry && entry[0]) {
		snprintf(path, sizeof(path), "%s%s.%s", m->dir, entry, MOD_LIB_EXT);
		if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE) {
			snprintf(m->libpath, sizeof(m->libpath), "%s", path);
		} else {
			warn("mod '%s': manifest entry \"%s\" has no %s.%s in the mod folder", m->id, entry, entry, MOD_LIB_EXT);
		}
		return;
	}

	snprintf(pattern, sizeof(pattern), "*.%s", MOD_LIB_EXT);
	if (!(list = SDL_GlobDirectory(m->dir, pattern, SDL_GLOB_CASEINSENSITIVE, &count))) {
		return;
	}
	if (count == 1) {
		snprintf(m->libpath, sizeof(m->libpath), "%s%s", m->dir, list[0]);
	} else if (count > 1) {
		/* Bundled dependency libraries look exactly like mod libraries, so
		 * picking one would be a coin flip. The mod's Lua half, if any,
		 * still loads. */
		warn("mod '%s': %d .%s files in the mod folder - add \"entry\" to %s to say which one is the mod", m->id, count,
		    MOD_LIB_EXT, MOD_MANIFEST);
	}
	SDL_free(list);
}

/* ------------------------------------------------------------------ */
/* user state (mods.json)                                              */
/* ------------------------------------------------------------------ */

static void state_path(char *buf, size_t bufsize, const char *root)
{
	snprintf(buf, bufsize, "%s%s", root, MOD_STATE);
}

/* Apply enabled/order from mods.json. Mods the file does not mention keep the
 * defaults the scan gave them, so dropping a folder in is enough to run it. */
static void state_apply(const char *root)
{
	char path[MAX_PATH];
	char *text;
	cJSON *json, *list;
	int i;

	state_path(path, sizeof(path), root);
	if (!(text = read_file(path))) {
		return;
	}
	json = cJSON_Parse(text);
	free(text);
	if (!json) {
		warn("mod loader: %s is not valid JSON - every mod stays enabled", path);
		return;
	}

	list = cJSON_GetObjectItemCaseSensitive(json, "mods");
	if (cJSON_IsObject(list)) {
		for (i = 0; i < mod_count; i++) {
			cJSON *entry = cJSON_GetObjectItemCaseSensitive(list, mods[i].id);
			cJSON *item;

			if (!cJSON_IsObject(entry)) {
				continue;
			}
			item = cJSON_GetObjectItemCaseSensitive(entry, "enabled");
			if (cJSON_IsBool(item)) {
				mods[i].enabled = cJSON_IsTrue(item) ? 1 : 0;
			}
			item = cJSON_GetObjectItemCaseSensitive(entry, "order");
			if (cJSON_IsNumber(item)) {
				mods[i].order = item->valueint;
			}
		}
	}
	cJSON_Delete(json);
}

/* Rewrite mods.json from the in-memory list, preserving entries for mods that
 * are not installed right now (a player who removes and re-adds a mod should
 * not silently get it re-enabled). Written temp-then-rename: a half-written
 * state file would disable every mod at the next launch. */
static int state_save(const char *root)
{
	char path[MAX_PATH], tmp[MAX_PATH];
	char *text, *existing;
	cJSON *json = NULL, *list;
	FILE *fp;
	int i, ok = 0;

	state_path(path, sizeof(path), root);

	if ((existing = read_file(path))) {
		json = cJSON_Parse(existing);
		free(existing);
	}
	if (!json) {
		json = cJSON_CreateObject();
	}
	if (!json) {
		return 0;
	}

	cJSON_DeleteItemFromObjectCaseSensitive(json, "version");
	if (!cJSON_AddNumberToObject(json, "version", 1)) {
		cJSON_Delete(json);
		return 0;
	}

	list = cJSON_GetObjectItemCaseSensitive(json, "mods");
	if (!cJSON_IsObject(list)) {
		cJSON_DeleteItemFromObjectCaseSensitive(json, "mods");
		list = cJSON_AddObjectToObject(json, "mods");
	}
	if (!list) {
		cJSON_Delete(json);
		return 0;
	}

	for (i = 0; i < mod_count; i++) {
		cJSON *entry = cJSON_CreateObject();

		if (!entry) {
			continue;
		}
		/* NB: a plain 1/0, not cJSON_True/cJSON_False - those are type
		 * bitmasks (2 and 1), so cJSON_False is truthy and would write
		 * every mod back as enabled. */
		cJSON_AddBoolToObject(entry, "enabled", mods[i].enabled ? 1 : 0);
		cJSON_AddNumberToObject(entry, "order", mods[i].order);
		cJSON_DeleteItemFromObjectCaseSensitive(list, mods[i].id);
		cJSON_AddItemToObject(list, mods[i].id, entry);
	}

	text = cJSON_Print(json);
	cJSON_Delete(json);
	if (!text) {
		return 0;
	}

	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	if ((fp = fopen(tmp, "w"))) {
		ok = (fputs(text, fp) >= 0);
		if (fclose(fp) != 0) {
			ok = 0;
		}
	}
	free(text);

	if (!ok) {
		warn("mod loader: could not write %s", tmp);
		remove(tmp);
		return 0;
	}
	if (!SDL_RenamePath(tmp, path)) {
		warn("mod loader: could not replace %s: %s", path, SDL_GetError());
		remove(tmp);
		return 0;
	}
	return 1;
}

/* Per-mod option values, stored alongside enabled/order in mods.json. Only
 * mods whose values the client owns use these - a native mod keeps its own and
 * persists them itself. Read on demand rather than cached: this runs a handful
 * of times at mod load, never in a frame. */
int mod_registry_get_option(const char *id, const char *key, int *out)
{
	char path[MAX_PATH];
	char *text;
	cJSON *json, *list, *entry, *opts, *item;
	int found = 0;

	state_path(path, sizeof(path), mod_root);
	if (!(text = read_file(path))) {
		return 0;
	}
	json = cJSON_Parse(text);
	free(text);
	if (!json) {
		return 0;
	}

	list = cJSON_GetObjectItemCaseSensitive(json, "mods");
	entry = cJSON_IsObject(list) ? cJSON_GetObjectItemCaseSensitive(list, id) : NULL;
	opts = cJSON_IsObject(entry) ? cJSON_GetObjectItemCaseSensitive(entry, "options") : NULL;
	item = cJSON_IsObject(opts) ? cJSON_GetObjectItemCaseSensitive(opts, key) : NULL;

	if (cJSON_IsNumber(item)) {
		*out = item->valueint;
		found = 1;
	} else if (cJSON_IsBool(item)) {
		*out = cJSON_IsTrue(item) ? 1 : 0;
		found = 1;
	}
	cJSON_Delete(json);
	return found;
}

int mod_registry_set_option(const char *id, const char *key, int value)
{
	char path[MAX_PATH], tmp[MAX_PATH];
	char *text, *existing;
	cJSON *json = NULL, *list, *entry, *opts;
	FILE *fp;
	int ok = 0;

	state_path(path, sizeof(path), mod_root);
	if ((existing = read_file(path))) {
		json = cJSON_Parse(existing);
		free(existing);
	}
	if (!json && !(json = cJSON_CreateObject())) {
		return 0;
	}

	list = cJSON_GetObjectItemCaseSensitive(json, "mods");
	if (!cJSON_IsObject(list)) {
		cJSON_DeleteItemFromObjectCaseSensitive(json, "mods");
		list = cJSON_AddObjectToObject(json, "mods");
	}
	entry = cJSON_IsObject(list) ? cJSON_GetObjectItemCaseSensitive(list, id) : NULL;
	if (!cJSON_IsObject(entry)) {
		entry = list ? cJSON_AddObjectToObject(list, id) : NULL;
	}
	opts = cJSON_IsObject(entry) ? cJSON_GetObjectItemCaseSensitive(entry, "options") : NULL;
	if (!cJSON_IsObject(opts)) {
		if (entry) {
			cJSON_DeleteItemFromObjectCaseSensitive(entry, "options");
		}
		opts = entry ? cJSON_AddObjectToObject(entry, "options") : NULL;
	}
	if (!opts) {
		cJSON_Delete(json);
		return 0;
	}
	cJSON_DeleteItemFromObjectCaseSensitive(opts, key);
	cJSON_AddNumberToObject(opts, key, value);

	text = cJSON_Print(json);
	cJSON_Delete(json);
	if (!text) {
		return 0;
	}

	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	if ((fp = fopen(tmp, "w"))) {
		ok = (fputs(text, fp) >= 0);
		if (fclose(fp) != 0) {
			ok = 0;
		}
	}
	free(text);
	if (!ok || !SDL_RenamePath(tmp, path)) {
		remove(tmp);
		return 0;
	}
	return 1;
}

int mod_registry_set_enabled(const char *id, int enabled)
{
	int i;

	for (i = 0; i < mod_count; i++) {
		if (!strcmp(mods[i].id, id)) {
			mods[i].enabled = enabled ? 1 : 0;
			return state_save(mod_root);
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* discovery                                                           */
/* ------------------------------------------------------------------ */

/* Read one candidate folder. Returns 1 if it is a mod (i.e. has a readable
 * manifest), 0 otherwise - the manifest is what separates a mod from whatever
 * else a player unzipped in there. */
static int read_mod(struct mod_desc *m, const char *root, const char *folder)
{
	char manifest[MAX_PATH];
	char entry[MOD_NAME_LEN] = "";
	char *text;
	cJSON *json;

	memset(m, 0, sizeof(*m));
	snprintf(m->dir, sizeof(m->dir), "%s%s/", root, folder);

	if (!has_file(m->dir, MOD_MANIFEST)) {
		return 0;
	}

	snprintf(manifest, sizeof(manifest), "%s%s", m->dir, MOD_MANIFEST);
	if (!(text = read_file(manifest))) {
		warn("mod loader: cannot read %s", manifest);
		return 0;
	}
	json = cJSON_Parse(text);
	free(text);
	if (!json) {
		warn("mod loader: %s is not valid JSON - skipping this mod", manifest);
		return 0;
	}

	/* Defaults first, manifest overrides. Only the file itself is mandatory:
	 * the mod.json that mod repos already ship for the launcher works as-is. */
	snprintf(m->id, sizeof(m->id), "%s", folder);
	json_str(json, "id", m->id, sizeof(m->id));
	if (!valid_id(m->id)) {
		warn("mod loader: %s has an unusable id - falling back to the folder name '%s'", manifest, folder);
		snprintf(m->id, sizeof(m->id), "%s", folder);
	}

	snprintf(m->name, sizeof(m->name), "%s", m->id);
	json_str(json, "name", m->name, sizeof(m->name));

	snprintf(m->version, sizeof(m->version), "unknown");
	json_str(json, "version", m->version, sizeof(m->version));

	json_str(json, "entry", entry, sizeof(entry));
	cJSON_Delete(json);

	m->enabled = 1;
	m->order = MOD_DEFAULT_ORDER;

	scan_library(m, entry);
	m->has_lua = scan_lua(m->dir);

	if (!m->libpath[0] && !m->has_lua) {
		warn("mod loader: '%s' has neither a .%s nor any .lua - skipping", m->id, MOD_LIB_EXT);
		return 0;
	}
	return 1;
}

static int mod_cmp(const void *a, const void *b)
{
	const struct mod_desc *x = a, *y = b;

	if (x->order != y->order) {
		return x->order < y->order ? -1 : 1;
	}
	/* ties broken by id so the load order is stable across filesystems */
	return strcmp(x->id, y->id);
}

int mod_registry_scan_root(const char *root)
{
	char **list;
	int count = 0, i;

	mod_count = 0;
	snprintf(mod_root, sizeof(mod_root), "%s", root);

	/* SDL_GetPrefPath creates the user dir but nothing creates mods/ under
	 * it, so a fresh install would never have one. */
	SDL_CreateDirectory(root);

	if (!(list = SDL_GlobDirectory(root, NULL, 0, &count))) {
		note("mod loader: no mods directory at %s (%s)", root, SDL_GetError());
		return 0;
	}

	for (i = 0; i < count; i++) {
		char path[MAX_PATH];
		SDL_PathInfo info;

		if (!strcmp(list[i], ".") || !strcmp(list[i], "..")) {
			continue;
		}
		snprintf(path, sizeof(path), "%s%s", root, list[i]);
		if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
			continue;
		}
		if (mod_count >= MOD_MAX) {
			warn("mod loader: more than %d mods in %s - ignoring the rest", MOD_MAX, root);
			break;
		}
		if (read_mod(&mods[mod_count], root, list[i])) {
			mod_count++;
		}
	}
	SDL_free(list);

	state_apply(root);
	qsort(mods, (size_t)mod_count, sizeof(mods[0]), mod_cmp);

	return mod_count;
}

int mod_registry_scan(void)
{
	return mod_registry_scan_root(client_mods_dir());
}
