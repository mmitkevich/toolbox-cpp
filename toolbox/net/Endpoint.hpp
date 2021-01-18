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
#include "toolbox/net/ParsedUrl.hpp"
#include <cstdint>
#include <sys/socket.h>
#include <toolbox/net/Protocol.hpp>
#include <toolbox/net/Sock.hpp>
#include <toolbox/util/TypeTraits.hpp>

#include <boost/asio/generic/basic_endpoint.hpp>
#include <boost/asio/ip/basic_endpoint.hpp>
#include <boost/asio/local/basic_endpoint.hpp>
#include <string_view>

namespace toolbox {
inline namespace net {

/*template <typename ProtocolT>
using BasicEndpoint = boost::asio::generic::basic_endpoint<ProtocolT>;

using DgramEndpoint = BasicEndpoint<DgramProtocol>;
using StreamEndpoint = BasicEndpoint<StreamProtocol>;
*/

template <typename ProtocolT>
using BasicIpEndpoint = boost::asio::ip::basic_endpoint<ProtocolT>;

using IpEndpoint = BasicIpEndpoint<IpProtocol>;
using UdpEndpoint = BasicIpEndpoint<UdpProtocol>;
using TcpEndpoint = BasicIpEndpoint<TcpProtocol>;

// TODO: rename StreamSock -> TcpSock, DgramSock -> UdpSock
using DgramEndpoint = UdpEndpoint;
using StreamEndpoint = TcpEndpoint;

template <typename ProtocolT>
using UnixEndpoint = boost::asio::local::basic_endpoint<ProtocolT>;

using UnixDgramEndpoint = UnixEndpoint<UnixDgramProtocol>;
using UnixStreamEndpoint = UnixEndpoint<UnixStreamProtocol>;

TOOLBOX_API AddrInfoPtr parse_endpoint(std::string_view uri, int type=0, int default_family=AF_UNSPEC);

template<typename EndpointT>
inline EndpointT parse_ip_endpoint(std::string_view uri, int type=0, int default_family=AF_UNSPEC) {
    const auto ai = parse_endpoint(uri, type, default_family);
    auto ipaddr = os::ip_address(ai->ai_addr);
    std::uint16_t port = os::port_number(ai->ai_addr);
    return EndpointT {ipaddr, port};
}

/*
inline DgramEndpoint parse_dgram_endpoint(const std::string& uri)
{
    const auto ai = parse_endpoint(uri, SOCK_DGRAM);
    return {ai->ai_addr, ai->ai_addrlen, ai->ai_protocol};
}*/

inline UdpEndpoint parse_dgram_endpoint(const std::string& uri) {
    return parse_ip_endpoint<UdpEndpoint>(uri);
}
/*
inline StreamEndpoint parse_stream_endpoint(const std::string& uri)
{
    const auto ai = parse_endpoint(uri, SOCK_STREAM);
    return {ai->ai_addr, ai->ai_addrlen, ai->ai_protocol};
}*/
inline TcpEndpoint parse_stream_endpoint(const std::string& uri) {
    return parse_ip_endpoint<TcpEndpoint>(uri);
}
class McastEndpoint : public BasicIpEndpoint<UdpProtocol> {
public:
    using Base = boost::asio::ip::basic_endpoint<UdpProtocol>;
    using Protocol = McastProtocol;
public:
    using Base::Base;
    const std::string& interface() const { return interface_name_; };
    McastEndpoint& interface(std::string_view name) { interface_name_ = name;  return *this; }
    int interface_index() const { return interface_index_; }
    McastEndpoint& interface(int index) { interface_index_ = index; return *this; }
private:
    std::string interface_name_;
    int interface_index_{-1};
};

template<typename ProtocolT>
IpEndpoint ip_endpoint(const BasicIpEndpoint<ProtocolT>& ep) {
    return IpEndpoint(ep.address(), ep.port());
}

TOOLBOX_API std::istream& operator>>(std::istream& is, DgramEndpoint& ep);
TOOLBOX_API std::istream& operator>>(std::istream& is, StreamEndpoint& ep);
TOOLBOX_API std::ostream& operator<<(std::ostream& os, const DgramEndpoint& ep);
TOOLBOX_API std::ostream& operator<<(std::ostream& os, const StreamEndpoint& ep);

template<typename EndpointT>
struct EndpointTraits {
    UdpEndpoint to_udp(const EndpointT& ep) {
        return ep;
    }
    TcpEndpoint to_tcp(const EndpointT& ep) {
        return ep;
    }
    McastEndpoint to_mcast(const EndpointT& ep) {
        return ep;
    }
};

} // namespace net
inline namespace util {
/*
template <>
struct TypeTraits<DgramEndpoint> {
    static auto from_string(std::string_view sv) { return parse_dgram_endpoint(std::string{sv}); }
    static auto from_string(const std::string& s) { return parse_dgram_endpoint(s); }
};
template <>
struct TypeTraits<StreamEndpoint> {
    static auto from_string(std::string_view sv) { return parse_stream_endpoint(std::string{sv}); }
    static auto from_string(const std::string& s) { return parse_stream_endpoint(s); }
};*/
template <typename ProtocolT>
struct TypeTraits<BasicIpEndpoint<ProtocolT>> {
    static auto from_string(std::string_view sv) { return from_string(std::string(sv)); }
    static auto from_string(const std::string& s, int type = SOCK_STREAM, int family=AF_INET) { 
        auto addrinfo = parse_endpoint(s, type, family);
        BasicIpEndpoint<ProtocolT> result {os::ip_address(addrinfo->ai_addr), os::port_number(addrinfo->ai_addr)};
        return result;
    }
};
template<>
struct TypeTraits<McastEndpoint> {
    static McastEndpoint from_string(std::string_view sv) { return from_string(std::string(sv)); }
    static McastEndpoint from_string(const std::string& s) { 
        ParsedUrl url {s};
        auto addrinfo = parse_endpoint(url.url(), SOCK_DGRAM, AF_UNSPEC);
        McastEndpoint result {os::ip_address(addrinfo->ai_addr), os::port_number(addrinfo->ai_addr)};
        result.interface(url.param("interface"));
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
        std::size_t operator()(toolbox::BasicIpEndpoint<ProtocolT> const& self) const {
            std::string_view sv((char*)self.data(), self.size());//*sizeof(typename toolbox::BasicIpEndpoint<ProtocolT>::data_type)
            return std::hash<std::string_view>{}(sv);
        }
    };

    template<>
    struct hash<toolbox::McastEndpoint> {
        std::size_t operator()(toolbox::McastEndpoint const& self) const {
            std::string_view sv((char*)self.data(), self.size());//*sizeof(typename toolbox::BasicIpEndpoint<ProtocolT>::data_type)
            return std::hash<std::string_view>{}(sv)
                ^ std::hash<std::string>{}(self.interface());
        }
    };
}
#endif // TOOLBOX_NET_ENDPOINT_HPP
