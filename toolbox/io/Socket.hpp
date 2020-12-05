#pragma once

#include "toolbox/io/PollHandle.hpp"
#include "toolbox/util/Slot.hpp"
#include "toolbox/sys/Error.hpp"
#include "toolbox/io/Buffer.hpp"
#include "toolbox/io/Reactor.hpp"

namespace toolbox {
inline namespace io {

enum class SockOpId {
    None    = 0,
    Connect = 1,
    Accept  = 2,
    Write   = 9,
    Read    = 17,
    Recv    = 18,
};

enum class SocketState {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    ConnFailed = 3,
    Failed = 4,
    Disconnecting = 5,
    Listening = 6
};

class SockOpBase {
public:
    SockOpBase() = default;
    explicit SockOpBase(SockOpId id)
    : id_(id) {}
protected:
    SockOpId id_ {SockOpId::None};    
};
template<typename...ArgsT>
class BasicSockOp : public SockOpBase {
public:    
    using Slot = util::Slot<ArgsT...>;
public:
    using SockOpBase::SockOpBase;
    BasicSockOp(SockOpId id, Slot slot)
    : SockOpBase(id) 
    , slot_(slot)
    {}
public:
    SockOpId id() const { return id_; }
    void complete(ArgsT... args) { 
        auto s = slot_;
        reset();
        s.invoke(std::forward<ArgsT>(args)...);
    }
    void reset() { *this = {}; }
    bool empty() const { return slot_.empty(); }
protected:
    Slot slot_ {};
};

using SockOp = BasicSockOp<std::error_code>;

template<typename BufferT>
class SockOpBuf : public BasicSockOp<ssize_t, std::error_code> {
    using Base = BasicSockOp<ssize_t, std::error_code>;
public:
    using Buffer = BufferT;
    using Slot = typename Base::Slot;
public:
    SockOpBuf() = default;
    SockOpBuf(SockOpId id, Slot slot, Buffer buf, int flags=0)
    : Base(id, slot)
    , buf_(buf)
    , flags_(flags) {}
public:
    using Base::complete;
    using Base::reset;
    using Base::empty;
    Buffer& buffer() { return buf_; } 
    int flags() { return flags_; }
public:
    Buffer buf_ {};
    int flags_ {};
};


template<typename SockT>
std::size_t read(SockT& sock, MutableBuffer& buffer, std::error_code& ec) {
    std::size_t total_bytes_read = 0;
    for(std::size_t i=0; buffer.size()>0; i++) {
        std::error_code ec {};
        ssize_t bytes_read = sock.read(buffer, ec);
        if(ec)
            break;
        assert(bytes_read >= 0);
        total_bytes_read += bytes_read;
        buffer += bytes_read;
    }
    return total_bytes_read;
}


/// BasiSocket = Socket + Sock impl
template<typename SockT, typename PollT, typename DerivedT>
class BasicSocket {
    using This = BasicSocket<SockT, PollT, DerivedT>;
public:
    using Sock = SockT;
    using PollHandle = PollT;
    using Protocol = typename Sock::Protocol;
    using Endpoint = typename Sock::Endpoint;
public:
    template<typename ReactorT>
    explicit BasicSocket(Sock&& sock, ReactorT& r)
    : sock_(std::move(sock))
    , poll_(*sock_, r.ctl(*sock_))
    {}

    template<typename ReactorT>
    explicit BasicSocket(Protocol protocol, ReactorT& r)
    : sock_(std::move(protocol))
    , poll_(*sock_, r.ctl(*sock_))
    {}

    template<typename ReactorT>
    explicit BasicSocket(ReactorT& r)
    : poll_(*sock_, r.ctl(*sock_))
    {}

    constexpr BasicSocket() noexcept = default;

    // Copy.
    BasicSocket(const BasicSocket&) = delete;
    BasicSocket& operator=(const BasicSocket&) = delete;

    // Move.
    BasicSocket(BasicSocket&&)  = default;
    BasicSocket& operator=(BasicSocket&&)  = default;

    ~BasicSocket() {
        close();
    }
    
    PollHandle& poll() { return poll_; }
    
    SocketState state() const { return state_; }
    void state(SocketState val) { state_ = val; }
    
    template<typename ReactorT>
    void open(Protocol protocol, ReactorT& r) {
        sock_ = SockT(protocol);
        poll_ = PollHandle {*sock_, r.ctl(*sock_)};
        impl().on_sock_prepare(sock_);
    }

    void open() {
        impl().on_sock_prepare(sock_);
    }

    void bind(const Endpoint& ep) { sock_.bind(ep); }
    
    // nothing by default, for tcp should be overriden
    void connect(const Endpoint& ep) { }

    void close() {
        if(!sock_.empty()) {
            poll_.reset();
            sock_.close();      
        }  
    }

    Sock& sock() { return sock_; }

    void read(MutableBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        //assert(state_==SocketState::Connected);
        if(!read_.empty()) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "read" };
        }
        read_ = {SockOpId::Read, slot, buffer};
        poll().add(PollEvents::Read);
    }

    void recv(MutableBuffer buffer, int flags, Slot<ssize_t, std::error_code> slot) {
        //assert(state_==SocketState::Connected);
        if(!read_.empty()) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "recv" };
        }
        read_ = {SockOpId::Recv, slot, buffer, flags};
        poll().add(PollEvents::Read);
    }

    void write(ConstBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        //assert(state_==SocketState::Connected);
        if(!write_.empty()) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "write" };
        }   
        write_ = {SockOpId::Write, slot, buffer, 0};
        poll().add(PollEvents::Write);
    } 

    void on_sock_prepare(Sock& sock) {
        sock.set_non_block();
    }
protected:
    DerivedT& impl() { return *static_cast<DerivedT*>(this); }
    
    /// io slot to use
    IoSlot io_slot() { return util::bind<&DerivedT::on_io_event>(this); }  

    /// basic io event handler
    void on_io_event(CyclTime now, os::FD fd, PollEvents events) {        
        bool again;
        do {
            again = false;
            // flush output
            if(events & PollEvents::Write) {
                while(!write_.empty()) {
                    std::error_code ec {};
                    ssize_t size = sock_.write(write_.buffer(), ec);
                    if(size<0 && ec.value()==EWOULDBLOCK) {
                        poll().add(PollEvents::Write);
                        break;
                    } else {
                        write_.complete(size, ec);
                        again = true;
                    }
                }
            }
            // then drain input
            if(events & PollEvents::Read) {
                while(!read_.empty()) {
                    ssize_t size;
                    std::error_code ec{};
                    switch(read_.id()) {
                        case SockOpId::Recv:
                            size = sock_.recv(read_.buffer(), read_.flags(), ec);
                            break;
                        default:
                            size = sock_.read(read_.buffer(), ec);
                            break;
                    }
                    if(size<0 && ec.value()==EWOULDBLOCK) {
                        poll().add(PollEvents::Read);
                        break;
                    } else {
                        read_.complete(size, ec);
                        again = true;
                    }
                }
            }
        } while(again);
        if(write_.empty()) {
            poll().del(PollEvents::Write);
        }
        //if(read_.empty()) {
        //    poll().del(PollEvents::Read);
        //}
        poll().commit();
    }
protected:
    SocketState state_ {SocketState::Disconnected};
    Sock sock_;
    PollHandle poll_;
    SockOpBuf<MutableBuffer> read_;
    SockOpBuf<ConstBuffer> write_;        
};

}
}