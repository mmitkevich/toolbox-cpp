#pragma once

#include "toolbox/net/Endpoint.hpp"
#include "toolbox/net/IpAddr.hpp"

namespace toolbox {

inline namespace net{

    struct EndpointFilter {
    optional<toolbox::net::IpAddrV4> host;
    optional<std::uint32_t> port;
    friend std::ostream& operator<<(std::ostream& os, const EndpointFilter& self) {
        return os << "host:"<<(self.host.has_value()?self.host.value().to_string():"none")
           << "," << "port:"<<(self.port.has_value()?std::to_string(self.port.value()):"none");
    }
};

struct EndpointsFilter {
    bool udp {true};
    bool tcp {true};
    EndpointFilter src;
    EndpointFilter dst;
    template<typename PacketT>
    bool operator()(const PacketT& packet) {
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
    friend std::ostream& operator<<(std::ostream& os, const EndpointsFilter& self) {
       return os<<"src:{"<<self.src<<"},dst:{"<<self.dst<<"}";
    }
};


} // ns net
} // ns toolbox