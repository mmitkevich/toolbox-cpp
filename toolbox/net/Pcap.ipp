#pragma once

#include "Pcap.hpp"

#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <net/ethernet.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

namespace toolbox{ inline namespace net {

inline PcapPacket::PcapPacket(const pcap_pkthdr* pkthdr, const u_char* packet)
: pkthdr(pkthdr)
, packet(packet)
{}

inline std::size_t PcapPacket::len() const
{
    return pkthdr->len;
}

inline const char* PcapPacket::data() const
{
    switch(ip_hdr()->ip_p) {
        case IPPROTO_TCP: 
            return (char*)(packet + sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct tcphdr));
        case IPPROTO_UDP:
            return (char*)(packet + sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr));
        default: 
            /*assert(false);*/
            return nullptr;
    }
}

inline std::size_t PcapPacket::size() const
{
    switch(ip_hdr()->ip_p) {
        case IPPROTO_TCP:
            return pkthdr->len - (sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct tcphdr));
        case IPPROTO_UDP:
            return pkthdr->len - (sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr));
        default:
            /*assert(false);*/
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

inline const struct tcphdr* PcapPacket::tcp_hdr() const
{
    return reinterpret_cast<const struct tcphdr*>(packet+sizeof(struct ether_header));
}

inline const struct udphdr* PcapPacket::udp_hdr() const
{
    return reinterpret_cast<const struct udphdr*>(packet+sizeof(struct ether_header));
}

inline IpProtocol PcapPacket::protocol() const
{
    int sock_type = 0;
    int ip_proto = ip_hdr()->ip_p;
    int family = AF_UNSPEC;
    switch(ether_hdr()->ether_type) {
        case ETHERTYPE_IP: family = AF_INET; break;
        case ETHERTYPE_IPV6: family = AF_INET6; break;
    }
    switch(ip_proto) {
        case IPPROTO_TCP: sock_type = SOCK_STREAM; break;
        case IPPROTO_UDP: sock_type = SOCK_DGRAM; break;
    }
    return IpProtocol(family, ip_proto, sock_type);
}

inline u_int PcapPacket::src_port() const
{
    int ip_proto = ip_hdr()->ip_p;
    switch(ip_proto) {
        case IPPROTO_TCP: return ntohs(tcp_hdr()->source);
        case IPPROTO_UDP: return ntohs(udp_hdr()->source);
        default: /*assert(false);*/ return 0;
    }
}

inline u_int PcapPacket::dst_port() const
{
    int ip_proto = ip_hdr()->ip_p;
    switch(ip_proto) {
        case IPPROTO_TCP: return ntohs(tcp_hdr()->dest);
        case IPPROTO_UDP: return ntohs(udp_hdr()->dest);
        default: /*assert(false);*/ return 0;
    }
}

inline std::string_view PcapPacket::src_host() const
{
     thread_local char buf[INET_ADDRSTRLEN] = "\0";
     inet_ntop(AF_INET, &(ip_hdr()->ip_src), buf, INET_ADDRSTRLEN);
     return buf;
}

inline std::string_view PcapPacket::dst_host() const
{
     thread_local char buf[INET_ADDRSTRLEN] = "\0";
     inet_ntop(AF_INET, &(ip_hdr()->ip_dst), buf, INET_ADDRSTRLEN);
     return buf;
}

}}