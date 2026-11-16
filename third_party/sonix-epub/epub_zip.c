#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "epub_zip.h"

#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include <string.h>

// PocketOS adaptation: reuse the SDK zlib, with bounded buffers and CRC checks.

// The end-of-central-directory record, and how far back from the end of the
// file it may sit. It is 22 bytes plus a comment of up to 64 KB; EPUBs have no
// comment, but reading the last 66 KB costs one seek and removes the question.
#define EOCD_SIGNATURE 0x06054b50u
#define EOCD_MIN 22u
#define EOCD_SEARCH (66u * 1024u)

#define CDH_SIGNATURE 0x02014b50u
#define CDH_MIN 46u
#define LFH_SIGNATURE 0x04034b50u
#define LFH_MIN 30u

// What this refuses to open at all. An EPUB with more than this many members,
// or a directory bigger than this, is not a book anyone is reading on a music
// player -- and the numbers exist so that a damaged file cannot ask for an
// arena the size of the card.
#define ZIP_MAX_ENTRIES 8192u
#define ZIP_MAX_DIRECTORY (4u * 1024u * 1024u)

static uint16_t read16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t read32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool read_at(FILE *f, long offset, void *into, size_t size) {
	return fseek(f, offset, SEEK_SET) == 0 && fread(into, 1, size, f) == size;
}

// Finds the end-of-central-directory record by scanning backwards for its
// signature. Backwards because a stored file could contain those four bytes,
// and the real record is the last one.
static bool find_eocd(FILE *f, long file_size, uint32_t *count, uint32_t *dir_size, uint32_t *dir_offset) {
	size_t window = (size_t)(file_size < (long)EOCD_SEARCH ? file_size : (long)EOCD_SEARCH);
	if (window < EOCD_MIN) {
		return false;
	}

	uint8_t *tail = malloc(window);
	if (!tail) {
		return false;
	}
	if (!read_at(f, file_size - (long)window, tail, window)) {
		free(tail);
		return false;
	}

	bool found = false;
	for (size_t back = EOCD_MIN; back <= window; back++) {
		const uint8_t *p = tail + window - back;
		if (read32(p) != EOCD_SIGNATURE || read16(p+4) || read16(p+6) ||
            read16(p+8)!=read16(p+10) || read16(p+20)!=back-EOCD_MIN) {
			continue;
		}
		*count = read16(p + 10);
		*dir_size = read32(p + 12);
		*dir_offset = read32(p + 16);
		found = true;
		break;
	}

	free(tail);
	return found;
}

bool epub_zip_open(epub_zip_t *zip, const char *path, epub_arena_t *arena) {
	memset(zip, 0, sizeof(*zip));
	zip->arena = arena;

	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    struct stat st;
    if (fd < 0) return false;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > 67108864) { close(fd); return false; }
    zip->file = fdopen(fd, "rb");
    if (!zip->file) close(fd);
	if (!zip->file) {
		return false;
	}
	if (fseek(zip->file, 0, SEEK_END) != 0) {
		epub_zip_close(zip);
		return false;
	}
	long file_size = ftell(zip->file);
	if (file_size < (long)EOCD_MIN) {
		epub_zip_close(zip);
		return false;
	}

	uint32_t count = 0, dir_size = 0, dir_offset = 0;
	if (!find_eocd(zip->file, file_size, &count, &dir_size, &dir_offset)) {
		epub_zip_close(zip);
		return false;
	}
	if (count == 0 || count > ZIP_MAX_ENTRIES || dir_size > ZIP_MAX_DIRECTORY ||
		(long)dir_offset + (long)dir_size > file_size) {
		epub_zip_close(zip);
		return false;
	}

	// The whole directory at once: it is small, and walking it with one read
	// per entry would be a few hundred seeks on a microSD.
	uint8_t *dir = malloc(dir_size);
	if (!dir) {
		epub_zip_close(zip);
		return false;
	}
	if (!read_at(zip->file, (long)dir_offset, dir, dir_size)) {
		free(dir);
		epub_zip_close(zip);
		return false;
	}

	zip->entries = arena_calloc(arena, count, sizeof(epub_zip_entry_t));
	// One block for every name, so a book with two hundred resources is one
	// allocation and not two hundred. dir_size bounds it: a name cannot be
	// longer than the record that holds it.
	zip->names = arena_alloc(arena, dir_size + 1u);
	if (!zip->entries || !zip->names) {
		free(dir);
		epub_zip_close(zip);
		return false;
	}

	uint32_t name_used = 0;
	uint32_t kept = 0, parsed = 0;
    uint64_t total_uncompressed = 0;
	size_t at = 0;
	for (uint32_t i = 0; i < count && at + CDH_MIN <= dir_size; i++) {
		const uint8_t *h = dir + at;
		if (read32(h) != CDH_SIGNATURE) {
			break;
		}
		uint16_t method = read16(h + 10);
		uint32_t comp = read32(h + 20);
		uint32_t uncomp = read32(h + 24);
		uint16_t name_len = read16(h + 28);
		uint16_t extra_len = read16(h + 30);
		uint16_t comment_len = read16(h + 32);
		uint32_t local = read32(h + 42);

		size_t record = CDH_MIN + (size_t)name_len + extra_len + comment_len;
		if (at + record > dir_size) {
			break;
		}

		// A directory entry, or something this cannot decompress. Skipped
		// rather than refused: a book with one odd member is still a book.
		bool unsafe = !name_len || name_len >= 1024 || h[CDH_MIN]=='/';
        for (unsigned n=0; n<name_len; n++) {
            unsigned char c=h[CDH_MIN+n];
            if (!c || c<' ' || c=='\\' || c==':') unsafe=true;
            if (c=='.' && n+1<name_len && h[CDH_MIN+n+1]=='.' &&
                (n==0 || h[CDH_MIN+n-1]=='/') && (n+2==name_len || h[CDH_MIN+n+2]=='/')) unsafe=true;
        }
        total_uncompressed += uncomp;
        if (unsafe || (read16(h+8)&1) || total_uncompressed>67108864 || comp>67108864 || local>=dir_offset) {
            free(dir); epub_zip_close(zip); return false;
        }
        bool usable = name_len > 0 && h[CDH_MIN + name_len - 1] != '/' && (method == 0 || method == 8);
		if (usable) {
			epub_zip_entry_t *e = &zip->entries[kept];
			e->name_offset = name_used;
			e->local_offset = local;
			e->comp_size = comp;
			e->uncomp_size = uncomp;
			e->method = method;
            e->crc = read32(h+16);

			memcpy(zip->names + name_used, h + CDH_MIN, name_len);
			name_used += name_len;
			zip->names[name_used++] = '\0';
			kept++;
		}

		at += record;
        parsed++;
	}

	free(dir);
	zip->count = kept;
	if (!kept || parsed!=count || at!=dir_size) {
		epub_zip_close(zip);
		return false;
	}
	return true;
}

void epub_zip_close(epub_zip_t *zip) {
	if (zip->file) {
		fclose(zip->file);
		zip->file = NULL;
	}
	// entries and names belong to the arena; whoever owns that frees them.
	zip->entries = NULL;
	zip->names = NULL;
	zip->count = 0;
}

const char *epub_zip_name(const epub_zip_t *zip, uint32_t index) {
	if (index >= zip->count) {
		return NULL;
	}
	return zip->names + zip->entries[index].name_offset;
}

int epub_zip_find(const epub_zip_t *zip, const char *name) {
	if (!name || !zip->count) {
		return -1;
	}
	for (uint32_t i = 0; i < zip->count; i++) {
		if (strcmp(zip->names + zip->entries[i].name_offset, name) == 0) {
			return (int)i;
		}
	}
	return -1;
}

uint32_t epub_zip_size(const epub_zip_t *zip, int index) {
	if (index < 0 || (uint32_t)index >= zip->count) {
		return 0;
	}
	return zip->entries[index].uncomp_size;
}

void *epub_zip_extract(const epub_zip_t *zip, int index, epub_arena_t *arena, uint32_t limit, uint32_t *size_out) {
	if (size_out) {
		*size_out = 0;
	}
	if (index < 0 || (uint32_t)index >= zip->count || !zip->file) {
		return NULL;
	}
	const epub_zip_entry_t *e = &zip->entries[index];
	if (e->uncomp_size > limit) {
		return NULL; // bigger than the caller is willing to hold
	}

	// The local header repeats the name and the extra field, and its lengths
	// are the ones that count: some writers put a different extra field here
	// than in the directory, so the data does not start where the directory's
	// lengths would say.
	uint8_t local[LFH_MIN];
	if (!read_at(zip->file, (long)e->local_offset, local, sizeof(local)) || read32(local) != LFH_SIGNATURE) {
		return NULL;
	}
	long data_at = (long)e->local_offset + (long)LFH_MIN + read16(local + 26) + read16(local + 28);

	// Room for a NUL after the data: every caller of this either parses text or
	// hands the bytes to an image decoder, and the first is much easier when
	// the buffer is already a C string.
	if ((read16(local+6)&1) || read16(local+8)!=e->method) return NULL;
    if (fseek(zip->file, 0, SEEK_END)) return NULL;
    long file_end=ftell(zip->file);
    if (data_at<0 || (uint64_t)data_at+e->comp_size>(uint64_t)file_end) return NULL;
    uint8_t *out = arena_alloc(arena, (size_t)e->uncomp_size + 1u);
	if (!out) {
		return NULL;
	}

	if (e->method == 0) {
		if (e->comp_size != e->uncomp_size || !read_at(zip->file, data_at, out, e->uncomp_size)) {
			return NULL;
		}
	} else {
		// The compressed bytes are read into a plain malloc and freed before
		// this returns. They are the one thing here that does not belong to the
		// book: keeping them in the arena would hold the compressed copy for as
		// long as the decompressed one.
		uint8_t *packed = malloc(e->comp_size ? e->comp_size : 1u);
		if (!packed) {
			return NULL;
		}
		if (!read_at(zip->file, data_at, packed, e->comp_size)) {
			free(packed);
			return NULL;
		}

        z_stream stream={0};
        stream.next_in=packed;stream.avail_in=e->comp_size;
        stream.next_out=out;stream.avail_out=e->uncomp_size+1;
        int initialized=inflateInit2(&stream,-MAX_WBITS)==Z_OK;
        int result=initialized?inflate(&stream,Z_FINISH):Z_DATA_ERROR;
        int ok=result==Z_STREAM_END && stream.total_out==e->uncomp_size && stream.total_in==e->comp_size;
        if(initialized)inflateEnd(&stream);
        free(packed);
        if(!ok)return NULL;
	}

    if ((uint32_t)crc32(0L,out,e->uncomp_size)!=e->crc) return NULL;
	out[e->uncomp_size] = '\0';
	if (size_out) {
		*size_out = e->uncomp_size;
	}
	return out;
}

