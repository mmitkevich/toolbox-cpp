#pragma once
#include "toolbox/Config.h"
#include "toolbox/net/Endpoint.hpp"
#include "toolbox/sys/Time.hpp"

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
    TOOLBOX_ALWAYS_INLINE TypedPacket(BinaryPacketT& impl)
    : impl_(impl){}

    TOOLBOX_ALWAYS_INLINE WallTime recv_timestamp() const { return impl_.recv_timestamp(); }
    TOOLBOX_ALWAYS_INLINE T* data() { return (T*)impl_.data(); }
    TOOLBOX_ALWAYS_INLINE const T* data() const { return (T*)impl_.data(); }
    TOOLBOX_ALWAYS_INLINE std::size_t size() const { return impl_.size()/sizeof(T); }

    TOOLBOX_ALWAYS_INLINE Endpoint src() const { return impl_.src(); }
    TOOLBOX_ALWAYS_INLINE Endpoint dst() const { return impl_.dst(); }

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