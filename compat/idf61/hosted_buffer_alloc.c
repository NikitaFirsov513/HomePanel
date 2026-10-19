#include <stdint.h>
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"

/* P4 SDMMC supports external buffers and performs cache synchronization itself.
 * PSRAM is not registered with MALLOC_CAP_DMA in IDF 6.1; requesting that
 * capability together with SPIRAM silently defeats Hosted's PSRAM preference.
 * Keep both the address and allocation extent aligned to the actual cache line.
 */
void *hosted_malloc_align(size_t size, size_t align)
{
    size_t cache_align = 0;
    if (!size || !align || (align & (align - 1))) return NULL;
    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cache_align) != ESP_OK) return NULL;
    if (cache_align > align) align = cache_align;
    if ((align & (align - 1)) || size > SIZE_MAX - (align - 1)) return NULL;
    const size_t extent = (size + align - 1) & ~(align - 1);
    void *ptr = heap_caps_aligned_alloc(align, extent, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) ptr = heap_caps_aligned_alloc(align, extent,
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    return ptr;
}
