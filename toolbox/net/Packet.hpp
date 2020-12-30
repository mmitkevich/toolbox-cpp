#pragma once
#include "toolbox/Config.h"
#include "toolbox/net/Endpoint.hpp"
#include "toolbox/sys/Time.hpp"

namespace toolbox { inline namespace net {

template<typename EndpointT>
class BasicPacketHeader {
public:
    using Endpoint = EndpointT;
public:
    BasicPacketHeader()
    {}
    BasicPacketHeader(Endpoint src, Endpoint dst)
    : src_(src), dst_(dst) {}
    BasicPacketHeader(Endpoint src, Endpoint dst, WallTime recv_ts)
    : src_(src), dst_(dst), recv_ts_(recv_ts) {}
    
    Endpoint& src() { return src_; }
    const Endpoint& src() const { return src_; }
    void src(const Endpoint& val) { src_ = val; }

    Endpoint& dst() { return dst_; }
    const Endpoint& dst() const { return dst_; }
    void dst(const Endpoint& val) { dst_ = val; }

    // received timestamp (from hardware?)
    const WallTime& recv_timestamp() const { return recv_ts_;}
    void recv_timestamp(const WallTime& val) { recv_ts_ = val;}
    
    friend std::ostream& operator<<(std::ostream& os, const BasicPacketHeader& self) {
        return os << "src:'"<<self.src() << "'"
           << ",dst:'"<<self.dst() << "',"
           << ",recv_ts:'"<<self.recv_timestamp()<<"'";
    }
protected:
    Endpoint src_{};
    Endpoint dst_{};
    WallTime recv_ts_{};
};

/// Packet = Header + Buffer
/// Buffer = ConstBuffer | MutableBuffer
template<typename BufferT, typename HeaderT>
class BasicPacket {
public:
    using Header = HeaderT;
    using Buffer = BufferT;
public:
    constexpr BasicPacket() = default;
    constexpr BasicPacket(Buffer&& buffer, Header&& header)
    : header_(header)
    , buffer_(buffer)
    {}
    explicit constexpr BasicPacket(Header&& header)
    : header_(header)
    {}
    explicit constexpr BasicPacket(Buffer&& buffer)
    : buffer_(buffer)
    {}  
    // Header
    Header& header() { return header_; }
    const Header& header() const { return header_; }

    // Buffer
    Buffer& buffer() { return buffer_; }
    const Buffer& buffer() const { return buffer_; }

    // to string_view
    std::string_view str() const { return {reinterpret_cast<const char*>(buffer().data()), buffer().size()}; }
protected:
    HeaderT header_;
    BufferT buffer_;
};

template<typename BufferT, typename EndpointT>
using Packet = BasicPacket<BufferT, BasicPacketHeader<EndpointT>>;

using UdpPacket = Packet<ConstBuffer, UdpEndpoint>;
using McastPacket = Packet<ConstBuffer, McastEndpoint>;

/// Typed packet view over arbitrary Packet
template<typename ValueT, typename PacketImplT>
class PacketView
{
public:
    using PacketImpl = PacketImplT;
    using Header = typename PacketImpl::Header;
    using Buffer = typename PacketImpl::Buffer;
    using value_type = ValueT;
public:
    TOOLBOX_ALWAYS_INLINE PacketView(const PacketImpl& impl)
    : impl_(impl){}

    TOOLBOX_ALWAYS_INLINE const value_type& value() const { return *reinterpret_cast<const value_type*>(impl_.buffer().data()); }
    TOOLBOX_ALWAYS_INLINE bool has_value() const { return impl_.size()>=sizeof(value_type); }

    // Header
    const Header& header() const { return impl_.header(); }
    Header& header() { return impl_.header(); }

    // Buffer
    Buffer& buffer() { return impl_.buffer(); }
    const Buffer& buffer() const { return impl_.buffer(); }
    
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
    const PacketImpl& impl_;
};


} // namespace net
} // namespace toolbox