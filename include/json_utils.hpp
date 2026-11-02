#pragma once
#define JSON_NOEXCEPTION 1
#include "json.hpp"
namespace gitdler {
inline std::string stringField(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}
}
