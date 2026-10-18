#pragma once
#include <string>
enum class PlanLoadResult { Missing, Loaded, Error };
PlanLoadResult loadFloorPlan(std::string &json);
bool saveFloorPlan(const std::string &json);
