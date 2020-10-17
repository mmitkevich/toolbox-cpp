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

#ifndef TOOLBOX_NET_ENDPOINT_HPP
#define TOOLBOX_NET_ENDPOINT_HPP

#include "toolbox/net/IpAddr.hpp"
#include <toolbox/net/Protocol.hpp>
#include <toolbox/net/Socket.hpp>
#include <toolbox/util/TypeTraits.hpp>

#include <boost/asio/generic/basic_endpoint.hpp>
#include <boost/asio/ip/basic_endpoint.hpp>
#include <boost/asio/local/basic_endpoint.hpp>
#include <string_view>

namespace toolbox {
inline namespace net {

template <typename ProtocolT>
using BasicEndpoint = boost::asio::generic::basic_endpoint<ProtocolT>;

using DgramEndpoint = BasicEndpoint<DgramProtocol>;
using StreamEndpoint = BasicEndpoint<StreamProtocol>;

template <typename ProtocolT>
using BasicIpEndpoint = boost::asio::ip::basic_endpoint<ProtocolT>;

using IpEndpoint = BasicIpEndpoint<IpProtocol>;
using UdpEndpoint = BasicIpEndpoint<UdpProtocol>;
using TcpEndpoint = BasicIpEndpoint<TcpProtocol>;


template <typename ProtocolT>
using UnixEndpoint = boost::asio::local::basic_endpoint<ProtocolT>;

using UnixDgramEndpoint = UnixEndpoint<UnixDgramProtocol>;
using UnixStreamEndpoint = UnixEndpoint<UnixStreamProtocol>;

TOOLBOX_API AddrInfoPtr parse_endpoint(const std::string& uri, int type=0, int default_family=AF_UNSPEC);

inline DgramEndpoint parse_dgram_endpoint(const std::string& uri)
{
    const auto ai = parse_endpoint(uri, SOCK_DGRAM);
    return {ai->ai_addr, ai->ai_addrlen, ai->ai_protocol};
}

inline StreamEndpoint parse_stream_endpoint(const std::string& uri)
{
    const auto ai = parse_endpoint(uri, SOCK_STREAM);
    return {ai->ai_addr, ai->ai_addrlen, ai->ai_protocol};
}

inline unsigned short get_port_number(struct sockaddr *sa) {
    switch(sa->sa_family) {
        case AF_INET: {
            struct sockaddr_in *addr = (struct sockaddr_in *) sa;
            return ntohs(addr->sin_port);
        }
        case AF_INET6: {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *) sa;
            return ntohs(addr->sin6_port);
        }
        default:
            assert(false);
            return 0;
    }
}

inline IpAddr get_ip_address(struct sockaddr *sa) {
    switch(sa->sa_family) {
        case AF_INET: {
            struct sockaddr_in *addr = (struct sockaddr_in *) sa;
            return IpAddrV4((IpAddrV4::uint_type)ntohl(addr->sin_addr.s_addr));
        }
        case AF_INET6:
            assert(false);
            return IpAddrV6{};  // FIXME
        default:
            assert(false);
            return IpAddr{};
    }
}

template<typename ProtocolT>
inline BasicIpEndpoint<ProtocolT> parse_ip_endpoint(const std::string& uri) {
    const auto ai = parse_endpoint(uri, 0);
    auto ipaddr = get_ip_address(ai->ai_addr);
    int port = get_port_number(ai->ai_addr);
    return {ipaddr, port};
}

TOOLBOX_API std::istream& operator>>(std::istream& is, DgramEndpoint& ep);
TOOLBOX_API std::istream& operator>>(std::istream& is, StreamEndpoint& ep);
TOOLBOX_API std::ostream& operator<<(std::ostream& os, const DgramEndpoint& ep);
TOOLBOX_API std::ostream& operator<<(std::ostream& os, const StreamEndpoint& ep);

} // namespace net
inline namespace util {

template <>
struct TypeTraits<DgramEndpoint> {
    static auto from_string(std::string_view sv) { return parse_dgram_endpoint(std::string{sv}); }
    static auto from_string(const std::string& s) { return parse_dgram_endpoint(s); }
};
template <>
struct TypeTraits<StreamEndpoint> {
    static auto from_string(std::string_view sv) { return parse_stream_endpoint(std::string{sv}); }
    static auto from_string(const std::string& s) { return parse_stream_endpoint(s); }
};
template <>
struct TypeTraits<IpEndpoint> {
    static IpEndpoint from_string(std::string_view sv) { return from_string(std::string(sv)); }
    static IpEndpoint from_string(const std::string& s) { 
        auto addrinfo = parse_endpoint(s, SOCK_DGRAM);
        IpEndpoint result {get_ip_address(addrinfo->ai_addr), get_port_number(addrinfo->ai_addr)};
        return result;
    }
};

} // namespace util
} // namespace toolbox

TOOLBOX_API std::ostream& operator<<(std::ostream& os, const sockaddr_in& sa);
TOOLBOX_API std::ostream& operator<<(std::ostream& os, const sockaddr_in6& sa);
TOOLBOX_API std::ostream& operator<<(std::ostream& os, const sockaddr_un& sa);
TOOLBOX_API std::ostream& operator<<(std::ostream& os, const sockaddr& sa);
TOOLBOX_API std::ostream& operator<<(std::ostream& os, const addrinfo& ai);

namespace std {
    template<typename ProtocolT>
    struct hash<toolbox::BasicIpEndpoint<ProtocolT>> {
        std::size_t operator()(toolbox::BasicIpEndpoint<ProtocolT> self) const {
            std::string_view sv((char*)self.data(), self.size());//*sizeof(typename toolbox::BasicIpEndpoint<ProtocolT>::data_type)
            return std::hash<std::string_view>{}(sv);
        }
    };
}
#endif // TOOLBOX_NET_ENDPOINT_HPP
