/*
 * Indexed sprite pack (.ugx) reader.
 *
 * One memory-mapped, read-only file per scale tier, shared by every thread:
 * no handles, no locks, one binary search per lookup. Payloads are untouched
 * PNG bytes. Format: Ugaris_HQ docs/architecture/sprite-pack-format.md
 * (ADR-0038). Written by Ugaris_Resources/tools/pack.py.
 *
 *   Header (64 bytes, little-endian)
 *     0  char[4] magic "UGX1"     4  u32 header_size (64)   8  u32 tier
 *     12 u32 count               16  u64 index_offset (64)  24 u64 data_offset
 *     32 u64 file_size           40  u32 index_crc32        44 u32 flags
 *   Index (count x 24 bytes, ids strictly ascending)
 *     0 u32 id  4 u32 length  8 u64 offset  16 u8 codec  17 u8 flags
 *     18 u16 width  20 u16 height  22 u16 reserved
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "dll.h"
#include "astonia.h"
#include "sdl/sdl.h"
#include "sdl/sdl_private.h"

#define UGX_HEADER 64
#define UGX_ENTRY  24

static uint32_t rd32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const unsigned char *p)
{
	return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static void unmap_file(struct sdl_pack *pk)
{
#ifdef _WIN32
	if (pk->base) {
		UnmapViewOfFile((LPCVOID)pk->base);
	}
	if (pk->map_handle) {
		CloseHandle((HANDLE)pk->map_handle);
	}
	if (pk->file_handle) {
		CloseHandle((HANDLE)pk->file_handle);
	}
#else
	if (pk->base) {
		munmap((void *)(uintptr_t)pk->base, pk->size);
	}
#endif
	pk->base = NULL;
	pk->map_handle = NULL;
	pk->file_handle = NULL;
	pk->size = 0;
}

// Map the whole file read-only. Returns -1 (silently) when the file does not exist.
static int map_file(struct sdl_pack *pk, const char *path)
{
#ifdef _WIN32
	HANDLE fh = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	LARGE_INTEGER sz;
	HANDLE mh;
	void *view;

	if (fh == INVALID_HANDLE_VALUE) {
		return -1;
	}
	if (!GetFileSizeEx(fh, &sz) || sz.QuadPart < UGX_HEADER) {
		CloseHandle(fh);
		return -1;
	}
	mh = CreateFileMappingA(fh, NULL, PAGE_READONLY, 0, 0, NULL);
	if (!mh) {
		CloseHandle(fh);
		return -1;
	}
	view = MapViewOfFile(mh, FILE_MAP_READ, 0, 0, 0);
	if (!view) {
		CloseHandle(mh);
		CloseHandle(fh);
		return -1;
	}
	pk->file_handle = fh;
	pk->map_handle = mh;
	pk->base = view;
	pk->size = (size_t)sz.QuadPart;
	return 0;
#else
	struct stat st;
	int fd = open(path, O_RDONLY);
	void *view;

	if (fd < 0) {
		return -1;
	}
	if (fstat(fd, &st) || st.st_size < UGX_HEADER) {
		close(fd);
		return -1;
	}
	view = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd); // the mapping keeps the file alive
	if (view == MAP_FAILED) {
		return -1;
	}
	pk->base = view;
	pk->size = (size_t)st.st_size;
	return 0;
#endif
}

int sdl_pack_open(struct sdl_pack *pk, const char *path)
{
	const unsigned char *h;
	uint32_t count, crc, prev;
	uint64_t index_offset, data_offset, file_size;
	size_t index_bytes;
	uint32_t n;

	memset(pk, 0, sizeof(*pk));
	if (map_file(pk, path)) {
		return -1;
	}
	h = pk->base;
	if (memcmp(h, "UGX1", 4) != 0 || rd32(h + 4) != UGX_HEADER) {
		warn("%s: not a sprite pack (bad magic)", path);
		goto bad;
	}
	pk->tier = rd32(h + 8);
	count = rd32(h + 12);
	index_offset = rd64(h + 16);
	data_offset = rd64(h + 24);
	file_size = rd64(h + 32);
	crc = rd32(h + 40);
	index_bytes = (size_t)count * UGX_ENTRY;

	if (file_size != (uint64_t)pk->size) {
		warn("%s: truncated sprite pack (%" PRIu64 " bytes expected, %zu on disk)", path, file_size, pk->size);
		goto bad;
	}
	if (index_offset != UGX_HEADER || data_offset < index_offset + index_bytes || data_offset > file_size) {
		warn("%s: corrupt sprite pack header", path);
		goto bad;
	}
	pk->index = pk->base + index_offset;
	if ((uint32_t)crc32(0L, pk->index, (uInt)index_bytes) != crc) {
		warn("%s: sprite pack index checksum mismatch", path);
		goto bad;
	}
	// Validate every entry once so lookups never have to.
	prev = 0;
	for (n = 0; n < count; n++) {
		const unsigned char *e = pk->index + (size_t)n * UGX_ENTRY;
		uint32_t id = rd32(e), len = rd32(e + 4);
		uint64_t off = rd64(e + 8);

		if ((n && id <= prev) || e[16] != 0 || off < data_offset || off + len > file_size) {
			warn("%s: corrupt sprite pack index entry %u (id %u)", path, n, id);
			goto bad;
		}
		prev = id;
	}
	pk->count = count;
	return 0;

bad:
	unmap_file(pk);
	memset(pk, 0, sizeof(*pk));
	return -1;
}

int sdl_pack_is_open(const struct sdl_pack *pk)
{
	return pk->base != NULL;
}

int sdl_pack_find(const struct sdl_pack *pk, unsigned int id, const unsigned char **data, uint32_t *len)
{
	uint32_t lo = 0, hi;

	if (!pk->base || !pk->count) {
		return -1;
	}
	hi = pk->count;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		const unsigned char *e = pk->index + (size_t)mid * UGX_ENTRY;
		uint32_t eid = rd32(e);

		if (eid == id) {
			*data = pk->base + rd64(e + 8);
			*len = rd32(e + 4);
			return 0;
		}
		if (eid < id) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return -1;
}

void sdl_pack_close(struct sdl_pack *pk)
{
	unmap_file(pk);
	memset(pk, 0, sizeof(*pk));
}
