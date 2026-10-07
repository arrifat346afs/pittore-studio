#pragma once
// Filter registry types. Split from engine/filter/filters.h.
#include <string>
#include <vector>

namespace pittore::filter {

struct FilterParam {
    const char *id = "";
    const char *label = "";
    double min = 0.0;
    double max = 100.0;
    double def = 0.0;
    int kind = 0;
    std::vector<const char *> choices;
    const char *suffix = "";
};

struct FilterDef {
    const char *id = "";
    const char *name = "";
    const char *category = "";
    std::vector<FilterParam> params;
};

}  // namespace pittore::filter
