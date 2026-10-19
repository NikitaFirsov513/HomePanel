#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
extern "C" void *hosted_malloc_align(size_t, size_t);
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #c); std::exit(1); } } while (false)
struct Call { size_t align, size; unsigned caps; };
static std::vector<Call> calls;
static bool external_fails = false, internal_fails = false, cache_fails = false;
static unsigned char storage;
extern "C" int esp_cache_get_alignment(unsigned caps, size_t *alignment) {
    CHECK(caps == MALLOC_CAP_SPIRAM); *alignment = 128; return cache_fails ? -1 : ESP_OK;
}
extern "C" void *heap_caps_aligned_alloc(size_t align, size_t size, unsigned caps) {
    calls.push_back({align, size, caps});
    return (caps & MALLOC_CAP_SPIRAM) ? external_fails ? nullptr : &storage : internal_fails ? nullptr : &storage;
}
int main() {
    CHECK(hosted_malloc_align(3858, 64) == &storage);
    CHECK(calls.size() == 1 && calls[0].align == 128 && calls[0].size == 3968);
    CHECK(calls[0].caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    calls.clear(); external_fails = true;
    CHECK(hosted_malloc_align(42, 64) == &storage && calls.size() == 2);
    CHECK(calls[1].caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    CHECK(calls[1].size == 128 && calls[1].align == 128);
    calls.clear(); internal_fails = true;
    CHECK(!hosted_malloc_align(1536, 64) && calls.size() == 2);
    calls.clear(); external_fails = internal_fails = false;
    CHECK(!hosted_malloc_align(SIZE_MAX, 64));
    CHECK(!hosted_malloc_align(0, 64)); CHECK(!hosted_malloc_align(1536, 3));
    CHECK(calls.empty()); cache_fails = true;
    CHECK(!hosted_malloc_align(1536, 64) && calls.empty());
    std::puts("Hosted PSRAM allocation tests passed");
}
