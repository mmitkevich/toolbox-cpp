#pragma once

#include "toolbox/util/Config.hpp"
#include <cstdint>
#include <string_view>

#include "../contrib/json/json.hpp"

namespace toolbox { 
namespace json {

using JsonDocument = nlohmann::json;
using JsonElement = nlohmann::json;
using JsonArray = nlohmann::json;
using JsonInt = std::int64_t;
using JsonString = std::string;
using JsonStringView = std::string_view;
using JsonError = std::out_of_range;

struct JsonParser {
    template<typename...ArgsT>
    auto parse(ArgsT...args) {
        return nlohmann::json::parse(std::forward<ArgsT>(args)...);
    }
};

}
}