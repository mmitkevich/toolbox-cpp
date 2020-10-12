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

class TOOLBOX_API PcapPacket {
public:
    using Endpoint = IpEndpoint;
public:
    PcapPacket(const pcap_pkthdr* pkthdr, const u_char* packet);
    
    // total length
    std::size_t len() const;

    // pointer to payload and payload size
    const char* data() const;

    std::size_t size() const;

    // protocol
    IpProtocol protocol() const;    

    // endpoints
    IpEndpoint src() const;
    IpEndpoint dst() const;

    friend std::ostream& operator<<(std::ostream& os, const PcapPacket& rhs) {
        switch(rhs.protocol().protocol()) {
            case IPPROTO_TCP: os << "tcp"; break;
            case IPPROTO_UDP: os << "udp"; break;
            case IPPROTO_PIM: os << "pim"; break;
            default: os << "proto("<<rhs.protocol().protocol() << ")"; break;
        }
        os << " ";
        os << rhs.src().address()<<":"<<rhs.src().port()<<" -> "<<rhs.dst().address()<<":"<<rhs.dst().port()<<" ";
        return os;
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
protected:
    const pcap_pkthdr* pkthdr;
    const u_char* packet;
};

struct HostPortFilter {
    optional<toolbox::net::IpAddrV4> host;
    optional<std::uint32_t> port;
    friend std::ostream& operator<<(std::ostream& os, const HostPortFilter& self) {
        return os << "host:"<<(self.host.has_value()?self.host.value().to_string():"none")
           << "," << "port:"<<(self.port.has_value()?std::to_string(self.port.value()):"none");
    }
};

struct HostPortFilters {
    bool udp {true};
    bool tcp {true};
    HostPortFilter src;
    HostPortFilter dst;
    template<typename PcapPacketT>
    bool operator()(const PcapPacketT& packet) {
        if(!udp && packet.protocol().protocol()==IPPROTO_UDP)
            return false;
        if(!tcp && packet.protocol().protocol()==IPPROTO_TCP)
            return false;
        if(src.port.has_value() && src.port.value() != packet.src().port())
            return false;
        if(src.host.has_value() && src.host.value() != packet.src().address())
            return false;
        if(dst.port.has_value() && dst.port.value() != packet.dst().port())
            return false;
        if(dst.host.has_value() && dst.host.value() != packet.dst().address())
            return false;
        return true;
    }
    friend std::ostream& operator<<(std::ostream& os, const HostPortFilters& self) {
       return os<<"src:{"<<self.src<<"},dst:{"<<self.dst<<"}";
    }
};


class TOOLBOX_API PcapDevice
{
public:
    ~PcapDevice() { close(); }
    void input(std::string_view input) { input_ = input; }
    std::string_view input() const { return input_; }
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