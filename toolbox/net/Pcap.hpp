#pragma once
#include <ostream>
#include <pcap/pcap.h>
#include <string_view>
#include <functional>
#include <iostream>
#include <pcap.h>
#include <sys/types.h>

#include "toolbox/net/Protocol.hpp"

extern "C" {
    struct ip;
    struct tcphdr;
    struct udphdr;
    struct ether_header;
};

namespace toolbox {
inline namespace net {

struct TOOLBOX_API PcapPacket {
    const pcap_pkthdr* pkthdr;
    const u_char* packet;

    PcapPacket(const pcap_pkthdr* pkthdr, const u_char* packet);
    std::size_t len() const;

    const struct ether_header* ether_hdr() const;
    const struct ip* ip_hdr() const;
    const struct tcphdr* tcp_hdr() const;
    const struct udphdr* udp_hdr() const;

    const char* data() const;
    std::size_t size() const;
    
    IpProtocol protocol() const;
    u_int src_port() const;
    u_int dst_port() const;
    std::string src_host() const;
    std::string dst_host() const;

    friend std::ostream& operator<<(std::ostream& os, const PcapPacket& rhs) {
        switch(rhs.protocol().protocol()) {
            case IPPROTO_TCP: os << "tcp"; break;
            case IPPROTO_UDP: os << "udp"; break;
            default: os << "proto("<<rhs.protocol().protocol() << ")"; break;
        }
        os << " ";
        os << rhs.src_host()<<":"<<rhs.src_port()<<" -> "<<rhs.dst_host()<<":"<<rhs.dst_port()<<" ";
        return os;
    }

};

class TOOLBOX_API PcapDevice {
public:
    using OnPacket = std::function<void(const PcapPacket& pkt)>;
public:
    PcapDevice(std::string_view file);
    ~PcapDevice();
    void on_packet(OnPacket on_packet);
    int loop();
    void max_packet_count(int cnt);
private:
    void packet_handler(const struct pcap_pkthdr* pkthdr, const u_char* packet);
    static void pcap_packet_handler(u_char *userData, const struct pcap_pkthdr* pkthdr, const u_char* packet);
private:
    pcap_t* pcap_file_{nullptr};
    OnPacket on_packet_{};
    int max_packet_count_{0};
};

} // namespace pcap
} // namespace toolbox