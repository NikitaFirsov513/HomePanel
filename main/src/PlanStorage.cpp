#include "PlanStorage.hpp"
#ifdef ESP_PLATFORM
#include "nvs.h"
#include "nvs_flash.h"
namespace {
bool init() { return nvs_flash_init() == ESP_OK; }
}
PlanLoadResult loadFloorPlan(std::string &json)
{
    if (!init()) return PlanLoadResult::Error;
    nvs_handle_t handle;
    const auto opened = nvs_open("floorplan", NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return PlanLoadResult::Missing;
    if (opened != ESP_OK) return PlanLoadResult::Error;
    size_t size = 0;
    auto error = nvs_get_blob(handle, "plan_v1", nullptr, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return PlanLoadResult::Missing; }
    if (error != ESP_OK || size == 0 || size > 32768) { nvs_close(handle); return PlanLoadResult::Error; }
    json.resize(size);
    error = nvs_get_blob(handle, "plan_v1", json.data(), &size);
    nvs_close(handle);
    return error == ESP_OK ? PlanLoadResult::Loaded : PlanLoadResult::Error;
}
bool saveFloorPlan(const std::string &json)
{
    if (json.empty() || json.size() > 32768 || !init()) return false;
    nvs_handle_t handle;
    if (nvs_open("floorplan", NVS_READWRITE, &handle) != ESP_OK) return false;
    auto error = nvs_set_blob(handle, "plan_v1", json.data(), json.size());
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}
#else
// Headless tests exercise persistence without accessing a board or real NVS.
static std::string stored;
PlanLoadResult loadFloorPlan(std::string &json) { json = stored; return stored.empty() ? PlanLoadResult::Missing : PlanLoadResult::Loaded; }
bool saveFloorPlan(const std::string &json) { if (json.empty()) return false; stored = json; return true; }
#endif
