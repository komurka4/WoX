#include "xeen/event_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb"); long length; uint8_t *bytes;
    CHECK(file); CHECK(fseek(file, 0, SEEK_END) == 0); length = ftell(file); CHECK(length >= 0);
    *size = (size_t)length; rewind(file); bytes = malloc(*size ? *size : 1); CHECK(bytes);
    CHECK(fread(bytes, 1, *size, file) == *size); CHECK(fclose(file) == 0); return bytes;
}
static void real_resource(const char *path, unsigned map, unsigned length, unsigned offset,
                          unsigned id, unsigned terminators, const char *prefix, unsigned selector)
{
    size_t size; uint8_t *archive_bytes = read_file(path, &size), storage[16000], ds[65536];
    xeen_archive archive; xeen_event_text_resource resource; xeen_resource_info info;
    CHECK(xeen_archive_open(&archive, archive_bytes, size) == XR_OK);
    CHECK(xeen_event_text_load(&resource, &archive, (uint16_t)map, storage, sizeof storage, &info) == XR_OK);
    CHECK(info.id == id && info.offset == offset && info.size == length);
    CHECK(resource.terminators == terminators && memcmp(storage, prefix, strlen(prefix)) == 0);
    memset(ds, 0xcc, sizeof ds);
    CHECK(xeen_event_text_install(&resource, ds, sizeof ds) == XE_OK && ds[0x97f4] == terminators);
    CHECK(xeen_event_text_copy(&resource, selector, ds, sizeof ds, 0xa827, 1000) == XE_OK);
    if (selector == 12) CHECK(memcmp(ds + 0xa827, prefix, strlen(prefix)) == 0);
    CHECK(ds[0xa827 + 999] == 0);
    free(archive_bytes);
}
int main(int argc, char **argv)
{
    uint8_t ds[65536], before[65536];
    const uint8_t text[] = {'a','b',0,'c',0};
    const uint8_t unterminated[] = {'x','y','z'};
    xeen_event_text_resource resource, saved;
    xeen_archive empty; const uint8_t empty_bytes[] = {0,0};
    memset(ds, 0xa5, sizeof ds);
    CHECK(xeen_event_text_bind(&resource, text, sizeof text) == XE_OK);
    CHECK(resource.terminators == 2 && resource.offsets[0] == 0 && resource.offsets[1] == 3 && resource.offsets[2] == 5);
    CHECK(xeen_event_text_install(&resource, ds, sizeof ds) == XE_OK);
    CHECK(ds[0x9a20] == 3 && ds[0x9a22] == 5 && ds[0x97f4] == 2);
    CHECK(xeen_event_text_copy(&resource, 255, ds, sizeof ds, 100, 8) == XE_OK);
    CHECK(memcmp(ds + 100, "ab\0\0\0\0\0\0", 8) == 0 && ds[99] == 0xa5 && ds[108] == 0xa5);
    CHECK(xeen_event_text_copy(&resource, 1, ds, sizeof ds, 200, 1) == XE_OK && ds[200] == 'c');
    CHECK(xeen_event_text_copy(&resource, 2, ds, sizeof ds, 200, 0) == XE_OK);
    /* Phase60: the selector one past the last text starts at the end of the
     * file (DOS memory beyond it in the original): an empty string. */
    CHECK(xeen_event_text_copy(&resource, 2, ds, sizeof ds, 200, 1) == XE_OK && ds[200] == 0);
    memcpy(before, ds, sizeof ds);
    CHECK(xeen_event_text_copy(&resource, 256, ds, sizeof ds, 200, 1) == XE_BAD_INPUT);
    CHECK(xeen_event_text_copy(&resource, 0, ds, sizeof ds, 65535, 2) == XE_TRUNCATED);
    CHECK(xeen_event_text_install(&resource, ds, 0x9a1e) == XE_TRUNCATED);
    CHECK(memcmp(before, ds, sizeof ds) == 0);
    CHECK(xeen_event_text_bind(&resource, unterminated, sizeof unterminated) == XE_OK);
    CHECK(xeen_event_text_copy(&resource, 0, ds, sizeof ds, 300, 3) == XE_OK && memcmp(ds + 300, "xyz", 3) == 0);
    memcpy(before, ds, sizeof ds);
    CHECK(xeen_event_text_copy(&resource, 0, ds, sizeof ds, 300, 4) == XE_TRUNCATED && memcmp(before, ds, sizeof ds) == 0);
    resource.offsets[0] = 4;
    CHECK(xeen_event_text_copy(&resource, 0, ds, sizeof ds, 300, 0) == XE_TRUNCATED);
    saved = resource;
    { uint8_t zeros[280] = {0}; CHECK(xeen_event_text_bind(&resource, zeros, sizeof zeros) == XE_CAPACITY); }
    CHECK(memcmp(&resource, &saved, sizeof resource) == 0);
    CHECK(xeen_archive_open(&empty, empty_bytes, sizeof empty_bytes) == XR_OK);
    CHECK(xeen_event_text_load(&resource, &empty, 29, ds, sizeof ds, NULL) == XR_NOT_FOUND);
    CHECK(memcmp(&resource, &saved, sizeof resource) == 0);
    CHECK(xeen_event_text_bind(&resource, NULL, 0) == XE_OK);
    CHECK(xeen_event_text_copy(&resource, 0, ds, sizeof ds, 0, 0) == XE_OK);
    CHECK(xeen_event_text_copy(&resource, 0, ds, sizeof ds, 0, 1) == XE_TRUNCATED);
    { xeen_event_text_resource overlap;
      CHECK(xeen_event_text_bind(&overlap, ds+100, 8)==XE_OK);
      memcpy(before,ds,sizeof ds);
      CHECK(xeen_event_text_install(&overlap,ds,sizeof ds)==XE_BAD_INPUT);
      CHECK(xeen_event_text_copy(&overlap,0,ds,sizeof ds,101,3)==XE_BAD_INPUT);
      CHECK(memcmp(before,ds,sizeof ds)==0);
    }
    if (argc == 3) {
        real_resource(argv[1], 29, 12147, 0x90f3a9, 0xbd11, 165, "Descend into the sewers?", 82);
        real_resource(argv[2], 2, 218, 0x4e778a, 0x7ca1, 3, "Saul\n\t125the Prayermaster", 12);
    } else CHECK(argc == 1);
    puts("event text tests passed"); return 0;
}
