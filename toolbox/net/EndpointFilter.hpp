#pragma once

#include "toolbox/net/Endpoint.hpp"
#include "toolbox/net/IpAddr.hpp"
#include "toolbox/util/TypeTraits.hpp"
#include <algorithm>
#include <optional>

namespace toolbox { inline namespace net {

struct EndpointFilter {
    std::optional<toolbox::net::IpAddr> address;
    std::optional<std::uint32_t> port;
    
    template<typename EndpointT>
    TOOLBOX_ALWAYS_INLINE bool match(const EndpointT& ep) const {
        
        if(port.has_value() && ep.port()!=port)
            return false;
        if(address.has_value() && ep.address()!=address)
            return false;
        return true;
    }
    friend std::ostream& operator<<(std::ostream& os, const EndpointFilter& self) {
        return os << "host:"<<(self.address.has_value()?self.address.value().to_string():"none")
           << "," << "port:"<<(self.port.has_value()?std::to_string(self.port.value()):"none");
    }
};

struct EndpointsFilter {
    bool tcp;
    bool udp;
    std::vector<EndpointFilter> sources;
    std::vector<EndpointFilter> destinations;
    
    template<typename EndpointT>
    TOOLBOX_ALWAYS_INLINE bool match(std::vector<EndpointFilter>& filters, const EndpointT& ep) {
        
        for(const auto &f: filters)
            if(f.match(ep))
                return true;
        return false;
    }
    template<typename HeaderT>
    TOOLBOX_ALWAYS_INLINE bool operator()(const HeaderT& header) {
        
        if(!udp && header.protocol().protocol()==IPPROTO_UDP)
            return false;
        if(!tcp && header.protocol().protocol()==IPPROTO_TCP)
            return false;

        if(!sources.empty() && !match(sources, header.src()))
            return false;

        if(!destinations.empty() && !match(destinations, header.dst()))
            return false;
        return true;
    }

    friend std::ostream& operator<<(std::ostream& os, const EndpointsFilter& self) {
       return os<<"src:[";
       for(auto& e:self.sources) {
           os<<"{"<<e<<"}";
       }
       os<<"],dst:[";
       for(auto& e:self.destinations) {
           os<<"{"<<e<<"}";
       }
       os<<"]";
       return os;
    }
};
}} // toolbox::net
namespace toolbox  { inline namespace util {
template <>
struct TypeTraits<EndpointFilter> {
    static EndpointFilter from_string(std::string_view sv) { return from_string(std::string(sv)); }
    static EndpointFilter from_string(const std::string& s) { 
        if(s[0]==':') {
            // :23 means port 23            
            return EndpointFilter {{}, TypeTraits<std::int32_t>::from_string(s.substr(1))};
        }else {
            // full url, possibly with port
            IpEndpoint ep = TypeTraits<IpEndpoint>::from_string(s);
            std::optional<std::uint32_t> port;
            if(ep.port()!=0)
                port = ep.port();
            return EndpointFilter {ep.address(), port };
        }
    }
};
}} // ns toolbox::util