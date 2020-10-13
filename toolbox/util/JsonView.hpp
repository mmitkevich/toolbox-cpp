#pragma once

#include "toolbox/util/Config.hpp"
#include <cstdint>
#include <string_view>

#define SIMDJSON_DLLIMPORTEXPORT TOOLBOX_API

#define SIMDJSON_IMPLEMENTATION_HASWELL 0
#define SIMDJSON_IMPLEMENTATION_WESTMERE 0
#define SIMDJSON_IMPLEMENTATION_FALLBACK 1

#include "../contrib/json/simdjson.h"

namespace toolbox { 
namespace jsonview {
using JsonParser = simdjson::dom::parser;
using JsonElement = simdjson::dom::element;
using JsonObject = simdjson::dom::object;
using JsonString = simdjson::padded_string;
using JsonError = simdjson::simdjson_error;
using JsonInt = std::int64_t;
using JsonUInt = std::uint64_t;
using JsonBool = bool;
using JsonStringView = std::string_view;
}
}