#pragma once
#include <stddef.h>
#define ESP_OK 0
#ifdef __cplusplus
extern "C" {
#endif
int esp_cache_get_alignment(unsigned caps, size_t *alignment);
#ifdef __cplusplus
}
#endif
