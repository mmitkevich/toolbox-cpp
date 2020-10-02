#pragma once
#include <ostream>
#include <pcap/pcap.h>
#include <stdexcept>
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

class PcapError : public std::runtime_error {
public:
    PcapError(const char* msg)
    : std::runtime_error(msg) {}
    PcapError(std::string msg)
    : std::runtime_error(msg) {}
};

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
    ~PcapDevice();
    void input(std::string_view input);
    std::string_view input() const { return input_; }
    void packet(OnPacket on_packet);
    void open();
    void close();
    int loop();
    void run();
    void max_packet_count(int cnt);
private:
    void packet_handler(const struct pcap_pkthdr* pkthdr, const u_char* packet);
    static void pcap_packet_handler(u_char *userData, const struct pcap_pkthdr* pkthdr, const u_char* packet);
private:
    pcap_t* handle_{nullptr};
    OnPacket on_packet_{};
    int max_packet_count_{0};
    std::string input_;
};

} // namespace pcap
} // namespace toolbox