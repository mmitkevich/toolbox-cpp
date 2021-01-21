#pragma once

#include "boost/beast/core/span.hpp"

namespace toolbox { inline namespace util {
template<typename T>
using Span = boost::beast::span<T>;
}}