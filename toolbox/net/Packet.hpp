#pragma once
#include "toolbox/Config.h"
#include "toolbox/net/Endpoint.hpp"
#include "toolbox/sys/Time.hpp"

namespace toolbox { inline namespace net {

template<typename EndpointT>
class Header {
public:
    using Endpoint = EndpointT;
public:
    Header()
    {}
    Header(Endpoint src, Endpoint dst)
    : src_(src), dst_(dst) {}
    Header(Endpoint src, Endpoint dst, WallTime recv_ts)
    : src_(src), dst_(dst), recv_ts_(recv_ts) {}
    
    const Endpoint& src() const { return src_; }
    void src(const Endpoint& val) { src_ = val; }

    const Endpoint& dst() const { return dst_; }
    void dst(const Endpoint& val) { dst_ = val; }

    const WallTime& recv_timestamp() const { return recv_ts_;}
    void recv_timestamp(const WallTime& val) { recv_ts_ = val;}
    
    friend std::ostream& operator<<(std::ostream& os, const Header& self) {
        return os << "src:'"<<self.src() << "'"
           << ",dst:'"<<self.dst() << "',"
           << ",recv_ts:'"<<self.recv_timestamp()<<"'";
    }
protected:
    Endpoint src_{};
    Endpoint dst_{};
    WallTime recv_ts_{};
};

/// Generic packet with externally stored data and custom header
template<
typename HeaderT>
class BinaryPacket {
public:
    using Header = HeaderT;
public:
    BinaryPacket(char* data=nullptr, std::size_t size=0)
    : data_(data)
    , size_(size) {}
    
    // Header
    Header& header() { return header_; }
    const Header& header() const { return header_; }

    // RawBytes
    char* data() { return data_; }
    const char* data() const { return data_; }
    std::size_t size() const { return size_; }
    std::string_view str() const { return {data_, size_}; }

    // Optional    
    std::string_view value() const { return str(); }
    bool has_value() const { return true; }
protected:
    HeaderT header_;
    char* data_{nullptr};
    std::size_t size_{};
};

/// Typed packet view over arbitrary BinaryPacket
template<
typename ValueT,
typename BinaryPacketT
> class PacketView
{
public:
    using BinaryPacket = BinaryPacketT;
    using Header = typename BinaryPacketT::Header;
    using value_type = ValueT;
public:
    TOOLBOX_ALWAYS_INLINE PacketView(const BinaryPacket& impl)
    : impl_(impl){}

    // Optional
    TOOLBOX_ALWAYS_INLINE const value_type& value() const { return *reinterpret_cast<const value_type*>(impl_.data()); }
    TOOLBOX_ALWAYS_INLINE bool has_value() const { return impl_.size()>=sizeof(value_type); }

    // Pointer
    TOOLBOX_ALWAYS_INLINE explicit operator const value_type*() {
        return reinterpret_cast<value_type*>(impl_.data());
    }
    TOOLBOX_ALWAYS_INLINE const value_type* operator->() {
        return reinterpret_cast<value_type*>(impl_.data());
    }

    // RawBytes
    TOOLBOX_ALWAYS_INLINE const char* data() const { return impl_.data(); }
    TOOLBOX_ALWAYS_INLINE std::size_t size() const { return impl_.size(); }
    TOOLBOX_ALWAYS_INLINE std::string_view str() const { return {data(), size()}; }

    // Header
    const Header& header() const { return impl_.header(); }
    Header& header() { return impl_.header(); }
    
    friend std::ostream& operator<<(std::ostream& os, const PacketView& self) {
        os << "header:"<< self.header()
           << "data:";
        if(self.impl_.size()>0)
            os << "{"<<*self.data()<<"}";
        else
            os << "null";
        return os;
    }
protected:
    const BinaryPacket& impl_;
};


} // namespace net
} // namespace toolbox