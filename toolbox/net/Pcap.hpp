#pragma once
#include <ostream>
#include <pcap/pcap.h>
#include <stdexcept>
#include <string_view>
#include <functional>
#include <iostream>
#include <pcap.h>
#include <sys/types.h>

#include <toolbox/sys/Log.hpp>
#include <toolbox/util/Slot.hpp>
#include "toolbox/net/Endpoint.hpp"
#include "toolbox/net/IpAddr.hpp"
#include "toolbox/net/Protocol.hpp"

#include "toolbox/sys/Time.hpp"
#include "toolbox/util/IntTypes.hpp"
#include "toolbox/util/String.hpp"

extern "C" {
    struct ip;
    struct ip6_hdr;
    struct tcphdr;
    struct udphdr;
    struct ether_header;
};

namespace toolbox {
inline namespace net {
namespace tbu = toolbox::util;

class PcapError : public std::runtime_error {
public:
    PcapError(const char* msg)
    : std::runtime_error(msg) {}
    PcapError(std::string msg)
    : std::runtime_error(msg) {}
};

class PcapPacket;

class PcapHeader {
public:
    using Endpoint = IpEndpoint;
public:
    PcapHeader(const PcapPacket& self);
    WallTime recv_timestamp() const;

    // endpoints
    IpEndpoint src() const;
    IpEndpoint dst() const;
    
    // protocol
    IpProtocol protocol() const; 

    friend std::ostream& operator<<(std::ostream& os, const PcapHeader& self) {
        int proto = self.protocol().protocol();
        os << "proto:";        
        switch(proto) {
            case IPPROTO_TCP: os << "tcp"; break;
            case IPPROTO_UDP: os << "udp"; break;
            case IPPROTO_PIM: os << "pim"; break;
            default: os << proto; break;
        }
        os << "src:'"<<self.src()<<"',dst:'"<<self.dst()<<"'";
        return os;
    }

private:
    const PcapPacket& self;
};

class TOOLBOX_API PcapPacket {
public:
    using Header = PcapHeader;
public:
    PcapPacket(const pcap_pkthdr* pkthdr, const u_char* packet);
    
    // RawData
    const char* data() const;
    std::size_t size() const;
    std::string_view str() const;

    // Header
    PcapHeader header() const { return PcapHeader(*this); }
        
    friend std::ostream& operator<<(std::ostream& os, const PcapPacket& self) {
        return os << "header:"<<self.header() << ",size="<<self.size();
    } 
protected:
    const struct ether_header* ether_hdr() const;
    const struct ip* ip_hdr() const;
    const struct ip6_hdr* ipv6_hdr() const;
    const struct tcphdr* tcp_hdr() const;
    const struct udphdr* udp_hdr() const;
    int protocol_family() const;
    unsigned short src_port() const;
    unsigned short dst_port() const;
    std::string src_host() const;
    std::string dst_host() const;
    std::size_t len() const;     
protected:
    const pcap_pkthdr* pkthdr;
    const u_char* packet;
    friend class PcapHeader;
};


class TOOLBOX_API PcapDevice
{
public:
    ~PcapDevice() { close(); }
    void input(std::string_view input) { input_ = input; }
    std::string input() const { return input_; }
    void open();
    void close();
    int loop();
    void run() {
        open();
        loop();
    }
    void max_packet_count(int val) { max_packet_count_ = val; }
    tbu::Signal<const PcapPacket&>& packets() { return packets_; }
private:
    static void pcap_packet_handler(u_char *userData, const struct pcap_pkthdr* pkthdr, const u_char* packet);
private:
    pcap_t* handle_{nullptr};
    int max_packet_count_{0};
    std::string input_;
    tbu::Signal<const PcapPacket&> packets_;
};

} // namespace pcap
} // namespace toolbox

#include "Pcap.ipp"