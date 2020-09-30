#include "toolbox/net/Pcap.hpp"

#include <stdexcept>
#include <cassert>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <net/ethernet.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

using namespace toolbox::net;

PcapPacket::PcapPacket(const pcap_pkthdr* pkthdr, const u_char* packet)
: pkthdr(pkthdr)
, packet(packet)
{}

std::size_t PcapPacket::len() const {
    return pkthdr->len;
}

const char* PcapPacket::data() const {
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

std::size_t PcapPacket::size() const {
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

const struct ether_header* PcapPacket::ether_hdr() const {
    return reinterpret_cast<const struct ether_header*>(packet);
}

const struct ip* PcapPacket::ip_hdr() const {
    return reinterpret_cast<const struct ip*>(packet+sizeof(struct ether_header));
}

const struct tcphdr* PcapPacket::tcp_hdr() const {
    return reinterpret_cast<const struct tcphdr*>(packet+sizeof(struct ether_header));
}

const struct udphdr* PcapPacket::udp_hdr() const {
    return reinterpret_cast<const struct udphdr*>(packet+sizeof(struct ether_header));
}

IpProtocol PcapPacket::protocol() const {
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

u_int PcapPacket::src_port() const {
    int ip_proto = ip_hdr()->ip_p;
    switch(ip_proto) {
        case IPPROTO_TCP: return ntohs(tcp_hdr()->source);
        case IPPROTO_UDP: return ntohs(udp_hdr()->source);
        default: /*assert(false);*/ return 0;
    }
}

u_int PcapPacket::dst_port() const {
    int ip_proto = ip_hdr()->ip_p;
    switch(ip_proto) {
        case IPPROTO_TCP: return ntohs(tcp_hdr()->dest);
        case IPPROTO_UDP: return ntohs(udp_hdr()->dest);
        default: /*assert(false);*/ return 0;
    }
}

std::string PcapPacket::src_host() const {
     char buf[INET_ADDRSTRLEN] = "\0";
     inet_ntop(AF_INET, &(ip_hdr()->ip_src), buf, INET_ADDRSTRLEN);
     return buf;
}

std::string PcapPacket::dst_host() const {
     char buf[INET_ADDRSTRLEN] = "\0";
     inet_ntop(AF_INET, &(ip_hdr()->ip_dst), buf, INET_ADDRSTRLEN);
     return buf;
}

PcapDevice::PcapDevice(std::string_view path)
{
    pcap_t *fp;
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_file_ = pcap_open_offline(path.data(), errbuf);
    if (pcap_file_ == NULL) {
	    throw std::runtime_error(std::string(errbuf));
    }
}

PcapDevice::~PcapDevice()
{
    assert(pcap_file_!=nullptr);
    pcap_close(pcap_file_);
}

void PcapDevice::on_packet(OnPacket on_packet)
{
    on_packet_ = on_packet;
}

void PcapDevice::max_packet_count(int cnt)
{
    max_packet_count_ = cnt;
}

void PcapDevice::pcap_packet_handler(u_char *userData, const struct pcap_pkthdr* pkthdr, const u_char* packet)
{
    ether_header *ethhdr = (ether_header*)packet;
    if(ntohs(ethhdr->ether_type) == ETHERTYPE_IP)
        reinterpret_cast<PcapDevice*>(userData)->packet_handler(pkthdr, packet);
    // NOTE: non-IP packets are dropped silently
}

void PcapDevice::packet_handler(const struct pcap_pkthdr* pkthdr, const u_char* packet)
{
    if(on_packet_)
        on_packet_(PcapPacket(pkthdr, packet));
}

int PcapDevice::loop()
{
    int rc = pcap_loop(pcap_file_, max_packet_count_, pcap_packet_handler, (u_char*) this);
    if(rc>=0) {
        return rc;
    }
    switch(rc) {
        case -1: throw std::runtime_error(std::string(pcap_geterr(pcap_file_)));
        case -2: return rc; // break called
        default: throw std::runtime_error("invalid pcap return code");
    }
}