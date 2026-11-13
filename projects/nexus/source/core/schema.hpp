#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace nexus {

    using json = nlohmann::json;

    enum class SType { Bool, Enum, Num, Color, Image, Str };

    struct SchemaEntry {
        std::string group;
        std::string path;
        std::string label;
        SType type;
        std::vector<std::string> opts;
        std::vector<std::string> optLabels;
        double min;
        double max;
        double step;
        json def;
        bool hidden;
        std::string unit; // "ms", "%", "px", "deg", "unit", "x", "%raw" oder leer
    };

}
