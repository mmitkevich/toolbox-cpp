#pragma once

#include "Pcap.hpp"
#include "toolbox/net/Endpoint.hpp"
#include "toolbox/net/IpAddr.hpp"
#include "toolbox/sys/Time.hpp"

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <net/ethernet.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace toolbox{ inline namespace net {

inline PcapPacket::PcapPacket(const pcap_pkthdr* pkthdr, const u_char* packet)
: pkthdr(pkthdr)
, packet(packet)
{}

inline WallTime PcapPacket::recv_timestamp() const {
    return toolbox::sys::to_time<WallClock>(pkthdr->ts);
}
inline std::size_t PcapPacket::len() const
{
    return pkthdr->len;
}

inline const char* PcapPacket::data() const
{
    int ip_p = ip_hdr()->ip_p;
    switch(ip_p) {
        case IPPROTO_TCP: 
            return (char*)(packet + sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct tcphdr));
        case IPPROTO_UDP: case IPPROTO_PIM:
            return (char*)(packet + sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr));
        default: 
            //assert(false);
            return nullptr;
    }
}

inline std::size_t PcapPacket::size() const
{
     switch(protocol_family()) {
        case AF_INET: {
            int ip_p = ip_hdr()->ip_p;
            switch(ip_p) {
                case IPPROTO_TCP:
                    return pkthdr->len - (sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct tcphdr));
                case IPPROTO_UDP:
                    return pkthdr->len - (sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr));
                default:
                    //assert(false);
                    return 0;
            }
        } break;
        case AF_INET6:
            assert(false);
            return 0;
        default:
            return 0;
     }
}

inline const struct ether_header* PcapPacket::ether_hdr() const
{
    return reinterpret_cast<const struct ether_header*>(packet);
}

inline const struct ip* PcapPacket::ip_hdr() const
{
    return reinterpret_cast<const struct ip*>(packet+sizeof(struct ether_header));
}

inline const struct ip6_hdr* PcapPacket::ipv6_hdr() const
{
    return reinterpret_cast<const struct ip6_hdr*>(packet+sizeof(struct ether_header));
}

inline const struct tcphdr* PcapPacket::tcp_hdr() const
{
    return reinterpret_cast<const struct tcphdr*>(packet+sizeof(struct ether_header)+sizeof(struct ip));
}

inline const struct udphdr* PcapPacket::udp_hdr() const
{
    return reinterpret_cast<const struct udphdr*>(packet+sizeof(struct ether_header)+sizeof(struct ip));
}

inline int PcapPacket::protocol_family() const {
    int family = AF_UNSPEC;
    u_int16_t ether_type = ntohs(ether_hdr()->ether_type);
    switch(ether_type) {
        case ETHERTYPE_IP: family = AF_INET; break;
        case ETHERTYPE_IPV6: family = AF_INET6; break;
    }
    return family;
}
inline IpProtocol PcapPacket::protocol() const
{
    int sock_type = 0;
    int ip_proto = ip_hdr()->ip_p;
    int family = protocol_family();
    switch(ip_proto) {
        case IPPROTO_TCP: sock_type = SOCK_STREAM; break;
        case IPPROTO_UDP: sock_type = SOCK_DGRAM; break;
    }
    return IpProtocol(family, ip_proto, sock_type);
}

inline unsigned short PcapPacket::src_port() const
{
    int ip_proto = ip_hdr()->ip_p;
    switch(ip_proto) {
        case IPPROTO_TCP: return ntohs(tcp_hdr()->source);
        case IPPROTO_UDP: return ntohs(udp_hdr()->source);
        default: /*assert(false);*/ return 0;
    }
}

inline unsigned short PcapPacket::dst_port() const
{
    int ip_proto = ip_hdr()->ip_p;
    switch(ip_proto) {
        case IPPROTO_TCP: return ntohs(tcp_hdr()->dest);
        case IPPROTO_UDP: return ntohs(udp_hdr()->dest);
        default: /*assert(false);*/ return 0;
    }
}

inline std::string PcapPacket::src_host() const
{
    char buf[INET_ADDRSTRLEN] = "\0";
    inet_ntop(AF_INET, &(ip_hdr()->ip_src), buf, INET_ADDRSTRLEN);
    return buf;
}

inline std::string PcapPacket::dst_host() const
{
    char buf[INET_ADDRSTRLEN] = "\0";
    inet_ntop(AF_INET, &(ip_hdr()->ip_dst), buf, INET_ADDRSTRLEN);
    return buf;
}

inline IpEndpoint PcapPacket::src() const 
{
    int family = protocol_family();
    switch(family) {
        case AF_INET: {
            using Addr = boost::asio::ip::address_v4;
            return {Addr((Addr::uint_type) ntohl(ip_hdr()->ip_src.s_addr)), src_port()};
        }
        case AF_INET6: {
            using Addr = boost::asio::ip::address_v6;
            Addr::bytes_type bytes;
            std::memcpy(bytes.data(), &ip_hdr()->ip_src.s_addr, bytes.size());
            return {Addr(bytes), src_port()};
        }
        default:
            return {IpProtocol{}, 0}; // AF_UNSPEC
    }
}
inline IpEndpoint PcapPacket::dst() const 
{
    int family = protocol_family();
    switch(family) {
        case AF_INET: {
            using Addr = boost::asio::ip::address_v4;
            return {Addr((Addr::uint_type) ntohl(ip_hdr()->ip_dst.s_addr)), dst_port()};
        }
        case AF_INET6: {
            using Addr = boost::asio::ip::address_v6;
            Addr::bytes_type bytes;
            std::memcpy(bytes.data(), &ip_hdr()->ip_dst.s_addr, bytes.size());
            return {Addr(bytes), dst_port()};
        }
        default:
            return {IpProtocol{}, 0}; // AF_UNSPEC
    }
}

}}