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
#include "toolbox/sys/Error.hpp"
#include <sys/socket.h>
#include <ifaddrs.h>
#include <sstream>

namespace toolbox {
inline namespace net {

using IpAddr = boost::asio::ip::address;
using IpAddrV4 = boost::asio::ip::address_v4;
using IpAddrV6 = boost::asio::ip::address_v6;
}
namespace os {

inline IpAddr ip_address(const struct sockaddr *sa) {
    switch(sa->sa_family) {
        case AF_INET: {
            const struct sockaddr_in *addr = (const struct sockaddr_in *) sa;
            return IpAddrV4((IpAddrV4::uint_type)ntohl(addr->sin_addr.s_addr));
        }
        case AF_INET6:
            assert(false);
            return IpAddrV6 {};  // FIXME
        default:
            assert(false);
            return IpAddr{};
    }
}

inline IpAddr ip_address(const struct addrinfo *ai) {
    switch(ai->ai_family) {
        case AF_INET: case AF_INET6: {
            return os::ip_address((struct sockaddr *)(ai->ai_addr));
        }
        default:
            assert(false);
            return IpAddr{};
    }
}

inline std::uint16_t port_number(const struct sockaddr *sa) {
    switch(sa->sa_family) {
        case AF_INET: {
            const struct sockaddr_in* addr = (const struct sockaddr_in *) sa;
            return ntohs(addr->sin_port);
        }
        case AF_INET6: {
            const struct sockaddr_in6* addr = (const struct sockaddr_in6 *) sa;
            return ntohs(addr->sin6_port);
        }
        default:
            assert(false);
            return 0;
    }
}

inline std::int16_t port_number(const struct addrinfo *ai) {
    switch(ai->ai_family) {
        case AF_INET: case AF_INET6: {
            return os::port_number((const struct sockaddr *)(ai->ai_addr));
        }
        default:
            assert(false);
            return 0;
    }
}
/// Returns the index of the network interface corresponding to the name ifname.
inline unsigned if_nametoindex(const char* ifname, std::error_code& ec) noexcept
{
    unsigned ifindex{0};
    if (ifname) {
        if (!(ifindex = ::if_nametoindex(ifname))) {
            ec = make_sys_error(errno);
        }
    }
    return ifindex;
}

/// Returns the index of the network interface corresponding to the name ifname.
inline unsigned if_nametoindex(const char* ifname)
{
    unsigned ifindex{0};
    if (ifname) {
        if (!(ifindex = ::if_nametoindex(ifname))) {
            std::stringstream ss;
            ss << "if_nametoindex" << ifname;
            throw std::system_error{make_sys_error(errno), ss.str()};
        }
    }
    return ifindex;
}

inline std::string if_addrtoname(const toolbox::IpAddr& addr) {
    struct ifaddrs *addrs, *iap;
    struct sockaddr_in *sa;
    
    std::string name;
    ::getifaddrs(&addrs);
    for (iap = addrs; iap != nullptr; iap = iap->ifa_next) {
        if (iap->ifa_addr && (iap->ifa_flags & IFF_UP)) {
            if(os::ip_address(iap->ifa_addr) == addr) {
                name = iap->ifa_name;
                break;
            }
        }
    }
    ::freeifaddrs(addrs);
    return name;
}

inline std::string if_addrtoname(std::string_view addr) {
    struct ifaddrs *addrs, *iap;
    struct sockaddr_in *sa;
    
    std::string name;
    ::getifaddrs(&addrs);
    for (iap = addrs; iap != nullptr; iap = iap->ifa_next) {
        if (iap->ifa_addr && (iap->ifa_flags & IFF_UP) && 
            (iap->ifa_addr->sa_family==AF_INET/* || iap->ifa_addr->sa_family==AF_INET6*/)) {
            if(os::ip_address(iap->ifa_addr).to_string() == addr) {
                name = iap->ifa_name;
                break;
            }
        }
    }
    ::freeifaddrs(addrs);
    return name;
}

} // namespace os
} // namespace toolbox

namespace toolbox {
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
