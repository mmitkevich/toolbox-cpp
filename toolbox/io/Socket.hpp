#pragma once

#include "toolbox/io/PollHandle.hpp"
#include "toolbox/util/Slot.hpp"
#include "toolbox/sys/Error.hpp"
#include "toolbox/io/Buffer.hpp"
#include "toolbox/io/Reactor.hpp"
#include "toolbox/net/Sock.hpp"
#include "toolbox/net/DgramSock.hpp"

namespace toolbox {
inline namespace io {

enum class SocketState {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    ConnFailed = 3,
    Failed = 4,
    Disconnecting = 5,
    Listening = 6
};

template<typename...ArgsT>
class CompletionSlot {
public:    
    using Slot = util::Slot<ArgsT...>;

public:
    void notify(ArgsT... args) { 
        auto s = slot_;
        reset();
        s.invoke(std::forward<ArgsT>(args)...);
    }
    explicit operator bool() {return !empty(); }
    bool empty() const { return slot_.empty(); }
    void reset() { slot_.reset(); }
    explicit operator bool() const { return (bool)slot_; }
    void set_slot(Slot val) { slot_ = val; }
protected:
    Slot slot_ {};
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

template<typename SocketT>
class SocketRead : public CompletionSlot<ssize_t, std::error_code> {
    using Base=CompletionSlot<ssize_t, std::error_code>;
  public:
    using Buffer = MutableBuffer;
    using Socket = SocketT;
    using Base::Base, Base::empty, Base::notify, Base::operator bool, Base::set_slot;

    bool prepare(Socket& socket, Buffer buf, int flags, Slot slot) {
        if(*this) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "read" };
        }
        buf_ = buf;
        flags_ = flags;
        set_slot(slot);
        return false;   // async
    }
    bool complete(Socket& socket, PollEvents events) {
        std::error_code ec{};
        ssize_t size = socket.recv(buf_, flags_, ec);
        if(size<0 && ec.value()==EWOULDBLOCK) {
            socket.poll().add(PollEvents::Read);
            return false; // no more
        } else {
            notify(size, ec);   // this will launch handlers they could make not-empty again
            if(empty()) {
                socket.poll().del(PollEvents::Read); // no write interest 
            }
            return true; // done
        }
    }
    void reset() { 
        Base::reset();
        buf_ = {};
        flags_ = 0;        
    }
  protected:
    Buffer buf_ {};
    int flags_ {};    
};

template<typename SocketT>
class SocketWrite : public CompletionSlot<ssize_t, std::error_code> {
    using Base=CompletionSlot<ssize_t, std::error_code>;
  public:
    using Buffer=ConstBuffer;
    using Socket=SocketT;
    using Base::Base, Base::empty, Base::notify, Base::operator bool, Base::set_slot;

    bool prepare(Socket& socket, Buffer buf, Slot slot) {
        if(*this) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "write" };
        }
        buf_ = buf;
        set_slot(slot);
        return false; // async
    }
    bool complete(Socket& socket, PollEvents events) {
        std::error_code ec{};
        ssize_t size = socket.write(buf_, ec);
        if(size<0 && ec.value()==EWOULDBLOCK) {
            socket.poll().add(PollEvents::Write);
            return false; // no more
        } else {
            notify(size, ec); // this will launch handlers they could make not-empty again
            if(empty()) {
                socket.poll().del(PollEvents::Write);
            }
            return true; // done
        }
    }
    void reset() { 
        Base::reset();
        buf_ = {};
    }
  protected:
    Buffer buf_ {};
};

template<typename SocketT>
class SockOpen {
public:
    void prepare(SocketT& socket) {
        socket.set_non_block();
        if (socket.is_ip_family()) {
            set_tcp_no_delay(socket.get(), true);
        }
    }
};

template<typename SockT>
class BasicSocket : public SockT {
    using Base = SockT;
    using This = BasicSocket<SockT>;
public:
    using typename SockT::Protocol;
    using typename SockT::Endpoint;
    using PollHandle = toolbox::PollHandle;
public:
    using Base::Base, Base::get;
    using Base::read, Base::write, Base::recv;

    template<typename ReactorT>
    explicit BasicSocket(ReactorT& r)
    : poll_(r.poll(*this))
    {
        open_.prepare(*this);
    }

    template<typename ReactorT>
    explicit BasicSocket(ReactorT& r, Protocol protocol)
    : Base(protocol)
    , poll_(r.poll(*this))
    {
        open_.prepare(*this);
    }

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
    void open(ReactorT& r, Protocol protocol) {
        *static_cast<SockT*>(this) = SockT(protocol);
        poll_ = r.poll(get());
        open_.prepare(*this);
    }
  
    void close() {
        poll_.reset();
        Base::close();        
    }

    void async_read(MutableBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        read_.prepare(*this, buffer, 0, slot);
    }
    
    void async_recv(MutableBuffer buffer, int flags, Slot<ssize_t, std::error_code> slot) {
        read_.prepare(*this, buffer, flags, slot);
    }

    void async_write(ConstBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        write_.prepare(*this, buffer, slot);
    } 

protected:
    void do_open() {
        poll().mod(io_slot());
        Base::set_non_block();
        sock_open(*this);
    }
    
    IoSlot io_slot() { return util::bind<&This::on_io_event>(this); }  

    void on_io_event(CyclTime now, os::FD fd, PollEvents events) {        
        for(std::size_t i=0; i<4; i++) {
            // read something
            if(events & PollEvents::Read) {
                while(read_) {
                    if(!read_.complete(*this, events)) {
                        events = events - PollEvents::Read; // read will block
                    }
                }
            }            
            // write ready things
            if(events & PollEvents::Write) {
                while(write_) {
                    if(!write_.complete(*this, events)) {
                        events = events - PollEvents::Write; // write will block
                        break;
                    }
                }
            }
            if(!(events & (PollEvents::Read+PollEvents::Write)))
                break; // nothing more to do
        }
        poll().commit();
    }
protected:
    SocketState state_ {SocketState::Disconnected};
    PollHandle poll_;
    SocketRead<This> read_;
    SocketWrite<This> write_;
    SockOpen<This> open_;
};

using DgramSocket = BasicSocket<DgramSock>;

}
}