#pragma once

#include "toolbox/net/Endpoint.hpp"

namespace toolbox { inline namespace net {

template<
typename EndpointT
> class BinaryPacket {
public:
    using Endpoint = EndpointT;
public:
    BinaryPacket(char* data=nullptr, std::size_t size=0)
    : data_(data)
    , size_(size) {}
    
    char* data() { return data_; }
    const char* data() const { return data_; }
    std::size_t size() const { return size_; }

    Endpoint src() { return src_; }
    Endpoint dst() { return dst_; }
protected:
    Endpoint src_{};
    Endpoint dst_{};
    char* data_{nullptr};
    std::size_t size_{};
};

/// Typed packet
template<
typename T,
typename BinaryPacketT = BinaryPacket<IpEndpoint> // untyped packet
> class TypedPacket
{
public:
    using Endpoint = typename BinaryPacketT::Endpoint;
    using BinaryPacket = BinaryPacketT;
    using value_type = T;
public:
    TypedPacket(BinaryPacketT& impl)
    : impl_(impl){}

    T* data() { return (T*)impl_.data(); }
    const T* data() const { return (T*)impl_.data(); }
    std::size_t size() const { return impl_.size()/sizeof(T); }

    Endpoint src() const { return impl_.src(); }
    Endpoint dst() const { return impl_.dst(); }

    friend std::ostream& operator<<(std::ostream& os, const TypedPacket& self) {
        os << "src:'"<<self.src() << "',"
           << "dst:'"<<self.dst() << "',"
           << "data:";
        if(self.impl_.size()>0)
            os << "{"<<*self.data()<<"}";
        else
            os << "null";
        return os;
    }
protected:
    BinaryPacketT& impl_;
};


} // namespace net
} // namespace toolbox