/*
 * Sprite pack (.ugx) reader tests: a pack written here from scratch (format per
 * Ugaris_HQ docs/architecture/sprite-pack-format.md), plus the real res/gx1.ugx
 * when present.
 */
#include "../src/astonia.h"
#include "../src/sdl/sdl_private.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <unistd.h>

static const unsigned char png_sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

static void put32(unsigned char *p, uint32_t v)
{
	p[0] = v & 255;
	p[1] = (v >> 8) & 255;
	p[2] = (v >> 16) & 255;
	p[3] = (v >> 24) & 255;
}
static void put64(unsigned char *p, uint64_t v)
{
	put32(p, (uint32_t)v);
	put32(p + 4, (uint32_t)(v >> 32));
}

// Write a pack with the given ids; payload of id N = PNG signature + N bytes of (N & 255).
static char *write_pack(const uint32_t *ids, uint32_t count, int corrupt_crc, int do_truncate)
{
	static char path[256];
	unsigned char *index = calloc(count, 24);
	uint64_t data_offset = ((64 + (uint64_t)count * 24 + 63) / 64) * 64;
	uint64_t pos = data_offset;
	FILE *f;
	uint32_t i;

	snprintf(path, sizeof(path), "/tmp/test_sprite_pack_%d.ugx", (int)getpid());
	f = fopen(path, "wb");
	if (!f) {
		return NULL;
	}
	fseek(f, (long)data_offset, SEEK_SET);
	for (i = 0; i < count; i++) {
		uint32_t len = 9 + ids[i] % 50;
		unsigned char *e = index + i * 24;
		unsigned char *payload = malloc(len);

		if (pos % 16) {
			unsigned char z[16] = {0};
			fwrite(z, 1, 16 - pos % 16, f);
			pos += 16 - pos % 16;
		}
		memcpy(payload, png_sig, 8);
		memset(payload + 8, ids[i] & 255, len - 8);
		fwrite(payload, 1, len, f);
		put32(e, ids[i]);
		put32(e + 4, len);
		put64(e + 8, pos);
		pos += len;
		free(payload);
	}
	{
		unsigned char h[64] = {0};
		uint32_t crc = (uint32_t)crc32(0L, index, count * 24);

		memcpy(h, "UGX1", 4);
		put32(h + 4, 64);
		put32(h + 8, 1);
		put32(h + 12, count);
		put64(h + 16, 64);
		put64(h + 24, data_offset);
		put64(h + 32, pos);
		put32(h + 40, corrupt_crc ? crc ^ 0xdeadbeef : crc);
		fseek(f, 0, SEEK_SET);
		fwrite(h, 1, 64, f);
		fwrite(index, 1, count * 24, f);
	}
	fclose(f);
	if (do_truncate) {
		if (truncate(path, (off_t)(pos - 5)) != 0) {
			return NULL;
		}
	}
	free(index);
	return path;
}

TEST(test_pack_roundtrip)
{
	static const uint32_t ids[] = {2, 5, 42, 57300, 100000, 1999999};
	struct sdl_pack pk;
	const unsigned char *data;
	uint32_t len, i;
	char *path = write_pack(ids, 6, 0, 0);

	ASSERT_PTR_NOT_NULL(path);
	ASSERT_EQ_INT(0, sdl_pack_open(&pk, path));
	ASSERT_TRUE(sdl_pack_is_open(&pk));
	ASSERT_EQ_INT(6, (int)pk.count);
	ASSERT_EQ_INT(1, (int)pk.tier);
	for (i = 0; i < 6; i++) {
		ASSERT_EQ_INT(0, sdl_pack_find(&pk, ids[i], &data, &len));
		ASSERT_EQ_INT((int)(9 + ids[i] % 50), (int)len);
		ASSERT_TRUE(memcmp(data, png_sig, 8) == 0);
		ASSERT_EQ_INT((int)(ids[i] & 255), data[len - 1]);
		ASSERT_TRUE(((uintptr_t)data - (uintptr_t)pk.base) % 16 == 0);
	}
	ASSERT_EQ_INT(-1, sdl_pack_find(&pk, 3, &data, &len));
	ASSERT_EQ_INT(-1, sdl_pack_find(&pk, 0, &data, &len));
	ASSERT_EQ_INT(-1, sdl_pack_find(&pk, 2000000, &data, &len));
	sdl_pack_close(&pk);
	ASSERT_FALSE(sdl_pack_is_open(&pk));
	ASSERT_EQ_INT(-1, sdl_pack_find(&pk, 2, &data, &len));
	remove(path);
}

TEST(test_pack_rejects_corruption)
{
	static const uint32_t ids[] = {1, 2, 3};
	struct sdl_pack pk;
	char *path = write_pack(ids, 3, 1, 0);

	ASSERT_PTR_NOT_NULL(path);
	ASSERT_EQ_INT(-1, sdl_pack_open(&pk, path));
	ASSERT_FALSE(sdl_pack_is_open(&pk));
	remove(path);

	ASSERT_EQ_INT(-1, sdl_pack_open(&pk, "/nonexistent/dir/gx9.ugx"));
	ASSERT_FALSE(sdl_pack_is_open(&pk));

	// a truncated file must be refused (file_size in the header disagrees)
	path = write_pack(ids, 3, 0, 1);
	ASSERT_PTR_NOT_NULL(path);
	ASSERT_EQ_INT(-1, sdl_pack_open(&pk, path));
	remove(path);
}

TEST(test_real_pack_if_present)
{
	struct sdl_pack pk;
	const unsigned char *data;
	uint32_t len;

	if (sdl_pack_open(&pk, "res/gx1.ugx")) {
		printf("  (res/gx1.ugx absent, skipped)\n");
		return;
	}
	ASSERT_TRUE(pk.count > 50000);
	ASSERT_EQ_INT(0, sdl_pack_find(&pk, 2, &data, &len)); // the "unknown sprite" placeholder
	ASSERT_TRUE(len > 8 && memcmp(data, png_sig, 8) == 0);
	ASSERT_EQ_INT(0, sdl_pack_find(&pk, 306, &data, &len)); // a patched base sprite
	sdl_pack_close(&pk);
}

TEST_MAIN(test_pack_roundtrip(); test_pack_rejects_corruption(); test_real_pack_if_present();)
