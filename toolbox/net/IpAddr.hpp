// The Reactive C++ Toolbox.
// Copyright (C) 2013-2019 Swirly Cloud Limited
// Copyright (C) 2020 Reactive Markets Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TOOLBOX_NET_IPADDR_HPP
#define TOOLBOX_NET_IPADDR_HPP

#include <boost/asio/ip/address.hpp>
#include "toolbox/util/TypeTraits.hpp"

namespace toolbox {
inline namespace net {

using IpAddr = boost::asio::ip::address;
using IpAddrV4 = boost::asio::ip::address_v4;
using IpAddrV6 = boost::asio::ip::address_v6;

} // namespace net

inline namespace util {
template <>
struct TypeTraits<IpAddrV4> {
    static auto from_string(std::string_view sv) { return boost::asio::ip::make_address_v4(std::string{sv}); }
    static auto from_string(const std::string& s) { return boost::asio::ip::make_address_v4(s); }
};
template <>
struct TypeTraits<IpAddrV6> {
    static auto from_string(std::string_view sv) { return boost::asio::ip::make_address_v6(std::string{sv}); }
    static auto from_string(const std::string& s) { return boost::asio::ip::make_address_v6(s); }
};
} // namespace util
} // namespace toolbox

#endif // TOOLBOX_NET_IPADDR_HPP
