/*
 * Test suite for the mod registry (folder discovery + mod.json + mods.json)
 *
 * Builds a throwaway mods tree under /tmp, scans it, and checks what the
 * loaders would see. The library files are empty - discovery never dlopens
 * anything, that is modder.c's job.
 *
 * Build: make test_mod_registry
 * Run: ./bin/test_mod_registry
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "modder/mod_registry.h"

#ifdef _WIN32
#define LIB_EXT "dll"
#elif defined(__APPLE__)
#define LIB_EXT "dylib"
#else
#define LIB_EXT "so"
#endif

/* ========== Stubs ========== */

int note(const char *format, ...)
{
	(void)format;
	return 0;
}

int warn(const char *format, ...)
{
	(void)format;
	return 0;
}

int fail(const char *format, ...)
{
	(void)format;
	return 0;
}

/* mod_registry_scan() resolves the root through the client; the tests drive
 * mod_registry_scan_root() directly and never reach this. */
const char *client_mods_dir(void)
{
	return "mods/";
}

/* ========== Fixture helpers ========== */

static char root[512];

static void rm_rf(const char *path)
{
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
	if (system(cmd) != 0) {
		/* nothing to clean up on the first run */
	}
}

static void fixture_begin(const char *name)
{
	snprintf(root, sizeof(root), "/tmp/ugaris-modreg-test-%s/", name);
	rm_rf(root);
	mkdir(root, 0755);
}

static void write_file(const char *relpath, const char *content)
{
	char path[1024];
	FILE *fp;

	snprintf(path, sizeof(path), "%s%s", root, relpath);
	fp = fopen(path, "w");
	if (!fp) {
		fprintf(stderr, "fixture: cannot write %s\n", path);
		exit(1);
	}
	if (content) {
		fputs(content, fp);
	}
	fclose(fp);
}

static void make_dir(const char *relpath)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s%s", root, relpath);
	mkdir(path, 0755);
}

static void make_lib(const char *dir, const char *base)
{
	char rel[1024];
	snprintf(rel, sizeof(rel), "%s/%s.%s", dir, base, LIB_EXT);
	write_file(rel, "");
}

/* Look a mod up by id rather than by index, so a test that is not about
 * ordering does not depend on it. */
static const struct mod_desc *find(const char *id)
{
	int i;
	for (i = 0; i < mod_registry_count(); i++) {
		const struct mod_desc *m = mod_registry_get(i);
		if (!strcmp(m->id, id)) {
			return m;
		}
	}
	return NULL;
}

/* ========== Tests ========== */

/* A folder is a mod because it has a mod.json - that is the whole rule. */
TEST(test_manifest_is_required)
{
	fixture_begin("required");
	make_dir("with-manifest");
	write_file("with-manifest/mod.json", "{\"name\":\"With\"}");
	make_lib("with-manifest", "anything");

	make_dir("no-manifest");
	make_lib("no-manifest", "orphan");

	write_file("loose-file.txt", "not a mod");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	ASSERT_PTR_NOT_NULL(find("with-manifest"));
}

/* Everything but the file itself is optional: the mod.json that mod repos
 * already ship for the launcher has no "id", and must still work. */
TEST(test_manifest_defaults)
{
	const struct mod_desc *m;

	fixture_begin("defaults");
	make_dir("tracker-mod");
	write_file("tracker-mod/mod.json", "{}");
	make_lib("tracker-mod", "whatever");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	m = mod_registry_get(0);
	ASSERT_PTR_NOT_NULL(m);
	ASSERT_TRUE(!strcmp(m->id, "tracker-mod")); /* from the folder name */
	ASSERT_TRUE(!strcmp(m->name, "tracker-mod")); /* name defaults to id */
	ASSERT_TRUE(!strcmp(m->version, "unknown"));
	ASSERT_EQ_INT(1, m->enabled);
	ASSERT_EQ_INT(MOD_DEFAULT_ORDER, m->order);
}

TEST(test_manifest_fields_win)
{
	const struct mod_desc *m;

	fixture_begin("fields");
	make_dir("folder-name");
	write_file("folder-name/mod.json", "{\"id\":\"real-id\",\"name\":\"Gains Tracker\",\"version\":\"1.2.0\"}");
	make_lib("folder-name", "lib");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	m = mod_registry_get(0);
	ASSERT_TRUE(!strcmp(m->id, "real-id"));
	ASSERT_TRUE(!strcmp(m->name, "Gains Tracker"));
	ASSERT_TRUE(!strcmp(m->version, "1.2.0"));
}

/* An id becomes a directory-ish key and a mods.json key; reject the ones that
 * would be trouble and keep the folder name instead. */
TEST(test_bad_id_falls_back_to_folder)
{
	fixture_begin("badid");
	make_dir("safe-folder");
	write_file("safe-folder/mod.json", "{\"id\":\"../../etc/passwd\"}");
	make_lib("safe-folder", "lib");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	ASSERT_TRUE(!strcmp(mod_registry_get(0)->id, "safe-folder"));
}

TEST(test_malformed_manifest_is_skipped)
{
	fixture_begin("malformed");
	make_dir("broken");
	write_file("broken/mod.json", "{ this is not json");
	make_lib("broken", "lib");

	ASSERT_EQ_INT(0, mod_registry_scan_root(root));
}

/* The library is found by extension, whatever it is called - the whole point
 * of dropping the bmod/cmod/... slot names. */
TEST(test_library_found_by_scan)
{
	fixture_begin("libscan");
	make_dir("m");
	write_file("m/mod.json", "{}");
	make_lib("m", "some_completely_arbitrary_name");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	ASSERT_TRUE(strstr(mod_registry_get(0)->libpath, "some_completely_arbitrary_name") != NULL);
}

/* Two libraries could be a mod plus a bundled dependency; picking one would be
 * a coin flip, so the native half is refused until "entry" says which. */
TEST(test_ambiguous_library_needs_entry)
{
	const struct mod_desc *m;

	fixture_begin("ambiguous");
	make_dir("m");
	write_file("m/mod.json", "{}");
	make_lib("m", "themod");
	make_lib("m", "libdependency");
	write_file("m/init.lua", "-- keeps the mod alive");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	m = mod_registry_get(0);
	ASSERT_EQ_INT(0, m->libpath[0]); /* no native half */
	ASSERT_EQ_INT(1, m->has_lua); /* but the Lua half still loads */
}

TEST(test_entry_resolves_ambiguity)
{
	fixture_begin("entry");
	make_dir("m");
	write_file("m/mod.json", "{\"entry\":\"themod\"}");
	make_lib("m", "themod");
	make_lib("m", "libdependency");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	ASSERT_TRUE(strstr(mod_registry_get(0)->libpath, "themod") != NULL);
}

/* A folder with a manifest but nothing to run is a packaging mistake, not a mod. */
TEST(test_empty_mod_is_skipped)
{
	fixture_begin("empty");
	make_dir("m");
	write_file("m/mod.json", "{\"name\":\"Nothing\"}");

	ASSERT_EQ_INT(0, mod_registry_scan_root(root));
}

TEST(test_lua_only_mod)
{
	const struct mod_desc *m;

	fixture_begin("luaonly");
	make_dir("lua-demo");
	write_file("lua-demo/mod.json", "{\"name\":\"Demo\"}");
	write_file("lua-demo/init.lua", "-- hi");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	m = mod_registry_get(0);
	ASSERT_EQ_INT(1, m->has_lua);
	ASSERT_EQ_INT(0, m->libpath[0]);
}

TEST(test_native_and_lua_in_one_mod)
{
	const struct mod_desc *m;

	fixture_begin("both");
	make_dir("hybrid");
	write_file("hybrid/mod.json", "{}");
	make_lib("hybrid", "hybrid");
	write_file("hybrid/init.lua", "-- hi");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	m = mod_registry_get(0);
	ASSERT_EQ_INT(1, m->has_lua);
	ASSERT_NE_INT(0, m->libpath[0]);
}

/* Unknown mods default to enabled, so dropping a folder in is enough. */
TEST(test_state_defaults_and_overrides)
{
	fixture_begin("state");
	make_dir("alpha");
	write_file("alpha/mod.json", "{}");
	make_lib("alpha", "a");
	make_dir("beta");
	write_file("beta/mod.json", "{}");
	make_lib("beta", "b");

	write_file("mods.json", "{\"version\":1,\"mods\":{\"alpha\":{\"enabled\":false,\"order\":5}}}");

	ASSERT_EQ_INT(2, mod_registry_scan_root(root));
	ASSERT_EQ_INT(0, find("alpha")->enabled);
	ASSERT_EQ_INT(5, find("alpha")->order);
	ASSERT_EQ_INT(1, find("beta")->enabled); /* unmentioned */
	ASSERT_EQ_INT(MOD_DEFAULT_ORDER, find("beta")->order);
}

/* order first, then id - so the load order does not depend on readdir(). */
TEST(test_sort_order)
{
	fixture_begin("sort");
	make_dir("zzz");
	write_file("zzz/mod.json", "{}");
	make_lib("zzz", "z");
	make_dir("aaa");
	write_file("aaa/mod.json", "{}");
	make_lib("aaa", "a");
	make_dir("mmm");
	write_file("mmm/mod.json", "{}");
	make_lib("mmm", "m");

	write_file("mods.json", "{\"mods\":{\"zzz\":{\"order\":1}}}");

	ASSERT_EQ_INT(3, mod_registry_scan_root(root));
	ASSERT_TRUE(!strcmp(mod_registry_get(0)->id, "zzz")); /* order 1 */
	ASSERT_TRUE(!strcmp(mod_registry_get(1)->id, "aaa")); /* order 100, id tiebreak */
	ASSERT_TRUE(!strcmp(mod_registry_get(2)->id, "mmm"));
}

TEST(test_set_enabled_round_trip)
{
	fixture_begin("toggle");
	make_dir("alpha");
	write_file("alpha/mod.json", "{}");
	make_lib("alpha", "a");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	ASSERT_EQ_INT(1, mod_registry_get(0)->enabled);

	ASSERT_EQ_INT(1, mod_registry_set_enabled("alpha", 0));
	ASSERT_EQ_INT(0, mod_registry_get(0)->enabled);

	/* survives a rescan, i.e. it really reached mods.json */
	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	ASSERT_EQ_INT(0, mod_registry_get(0)->enabled);

	ASSERT_EQ_INT(1, mod_registry_set_enabled("alpha", 1));
	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	ASSERT_EQ_INT(1, mod_registry_get(0)->enabled);
}

TEST(test_set_enabled_unknown_mod)
{
	fixture_begin("unknown");
	make_dir("alpha");
	write_file("alpha/mod.json", "{}");
	make_lib("alpha", "a");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	ASSERT_EQ_INT(0, mod_registry_set_enabled("does-not-exist", 0));
}

/* A player who removes a mod and puts it back should not silently get it
 * re-enabled, so entries for absent mods survive a rewrite. */
TEST(test_state_preserves_absent_mods)
{
	fixture_begin("absent");
	make_dir("alpha");
	write_file("alpha/mod.json", "{}");
	make_lib("alpha", "a");

	write_file("mods.json", "{\"version\":1,\"mods\":{\"ghost\":{\"enabled\":false,\"order\":7}}}");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	ASSERT_EQ_INT(1, mod_registry_set_enabled("alpha", 0));

	/* bring the ghost back */
	make_dir("ghost");
	write_file("ghost/mod.json", "{}");
	make_lib("ghost", "g");

	ASSERT_EQ_INT(2, mod_registry_scan_root(root));
	ASSERT_EQ_INT(0, find("ghost")->enabled);
	ASSERT_EQ_INT(7, find("ghost")->order);
}

/* A corrupt state file must not take every mod down with it. */
TEST(test_corrupt_state_leaves_mods_enabled)
{
	fixture_begin("corruptstate");
	make_dir("alpha");
	write_file("alpha/mod.json", "{}");
	make_lib("alpha", "a");
	write_file("mods.json", "{ not json at all");

	ASSERT_EQ_INT(1, mod_registry_scan_root(root));
	ASSERT_EQ_INT(1, mod_registry_get(0)->enabled);
}

/* The old six-slot ceiling is the thing this rework exists to remove. */
TEST(test_more_than_six_mods)
{
	int i;

	fixture_begin("many");
	for (i = 0; i < 12; i++) {
		char dir[64];
		snprintf(dir, sizeof(dir), "mod%02d", i);
		make_dir(dir);
		{
			char rel[128];
			snprintf(rel, sizeof(rel), "%s/mod.json", dir);
			write_file(rel, "{}");
		}
		make_lib(dir, "lib");
	}

	ASSERT_EQ_INT(12, mod_registry_scan_root(root));
}

TEST(test_missing_root_is_not_fatal)
{
	snprintf(root, sizeof(root), "/tmp/ugaris-modreg-test-nonexistent-%d/", (int)getpid());
	rm_rf(root);

	/* the scan creates the directory and finds nothing in it */
	ASSERT_EQ_INT(0, mod_registry_scan_root(root));
	rm_rf(root);
}

TEST_MAIN(test_manifest_is_required(); test_manifest_defaults(); test_manifest_fields_win();
    test_bad_id_falls_back_to_folder(); test_malformed_manifest_is_skipped(); test_library_found_by_scan();
    test_ambiguous_library_needs_entry(); test_entry_resolves_ambiguity(); test_empty_mod_is_skipped();
    test_lua_only_mod(); test_native_and_lua_in_one_mod(); test_state_defaults_and_overrides(); test_sort_order();
    test_set_enabled_round_trip(); test_set_enabled_unknown_mod(); test_state_preserves_absent_mods();
    test_corrupt_state_leaves_mods_enabled(); test_more_than_six_mods(); test_missing_root_is_not_fatal();)
