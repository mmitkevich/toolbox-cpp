#pragma once

#include "toolbox/io/PollHandle.hpp"
#include "toolbox/util/Slot.hpp"
#include "toolbox/sys/Error.hpp"
#include "toolbox/io/Buffer.hpp"
#include "toolbox/io/Reactor.hpp"
#include "toolbox/net/Sock.hpp"
#include "toolbox/net/DgramSock.hpp"
#include <system_error>

namespace toolbox {
inline namespace io {

enum class SocketState {
    Closed = 0,
    Connecting = 1,
    Open = 2,
    Failed = 3,
    Closing = 4,
    Listening = 5
};

template<typename...ArgsT>
class CompletionSlot {
public:    
    using SlotImpl = util::Slot<ArgsT...>;
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
    void set_slot(SlotImpl val) { slot_ = val; }
protected:
    SlotImpl slot_ {};
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

template<typename SocketT, typename EndpointT>
class SocketRead : public CompletionSlot<ssize_t, std::error_code> {
    using This = SocketRead<SocketT, EndpointT>;
    using Base = CompletionSlot<ssize_t, std::error_code>;
  public:
    using Socket = SocketT;
    using Endpoint = EndpointT;
    using typename Base::SlotImpl;
    using Base::Base, Base::empty, Base::notify, Base::operator bool, Base::set_slot;

    void flags(int val) { flags_ = val; }
    void endpoint(Endpoint* ep) { endpoint_ = ep; }
    bool prepare(Socket& socket, SlotImpl slot, MutableBuffer buf) {
        if(*this) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "read" };
        }
        buf_ = buf;
        set_slot(slot);
        socket.poll().add(PollEvents::Read);
        socket.poll().commit();
        return false;   // async
    }
    
    void notify(ssize_t size, std::error_code ec) { 
        auto s = slot_;
        reset();
        s.invoke(size, ec);
    }

    bool complete(Socket& socket, PollEvents events) {
        std::error_code ec{};
        ssize_t size;
        size = socket.recv(buf_, flags_, ec);
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
        endpoint_ = {};
    }
  protected:
    MutableBuffer buf_ {};
    int flags_ {};    
    Endpoint* endpoint_ {};
};

enum SocketFlags {
    ZeroCopy = 1
};

template<typename SocketT, typename EndpointT>
class SocketWrite : public CompletionSlot<ssize_t, std::error_code> {
    using This = SocketWrite<SocketT, EndpointT>;
    using Base = CompletionSlot<ssize_t, std::error_code>;
  public:
    using Socket = SocketT;

    using Endpoint = EndpointT;
    using typename Base::SlotImpl;

    using Base::Base, Base::empty, Base::notify, Base::operator bool, Base::set_slot;

    void flags(int val) { flags_ = val; }
    void endpoint(const Endpoint* ep) { endpoint_ = ep; }

    bool prepare(Socket& socket, SlotImpl slot, ConstBuffer data) {
        if(*this) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "write" };
        }
        data_ = data;
        set_slot(slot);
        socket.poll().add(PollEvents::Write);
        return false; // async
    }
    // zero copy
    bool prepare(Socket& socket, SlotImpl slot, std::size_t size, util::Slot<void*, std::size_t> mut) {
         if(*this) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "write" };
        }
        data_ = {nullptr, size}; // zero copy
        set_mut(mut);
        set_slot(slot);
        socket.poll().add(PollEvents::Write);
        return false;
    }
    void set_mut(Slot<void*, std::size_t> mut) {
        mut_ = mut;
    }
    void notify(ssize_t size, std::error_code ec) { 
        auto s = slot_;
        reset();
        s.invoke(size, ec);
    }
    bool complete(Socket& socket, PollEvents events) {
        std::error_code ec {};
        ssize_t size;
        if(!data_.data()) {
            auto wbuf = buf_.prepare(data_.size());
            mut_(wbuf.data(), wbuf.size());
            size = socket.write(wbuf, ec);
        }else {
            size = socket.write(data_, ec);
        }

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
        data_ = {};
        endpoint_ = {};
        flags_ = 0;
    }
  protected:
    ConstBuffer data_ {};
    const Endpoint* endpoint_ {};
    int flags_ {};
    Buffer buf_ {};
    util::Slot<void*, std::size_t> mut_;
};


template<typename SocketT>
class SockOpen {
public:
    void prepare(SocketT& socket) {
        socket.set_non_block();
    }
};

template<typename SocketStateT>
using BasicSocketStateSignal = Signal<SocketStateT, SocketStateT, std::error_code>;

template<typename DerivedT, typename SockT, typename StateT>
class BasicSocket : public SockT {
    using Base = SockT;
    using This = BasicSocket<DerivedT, SockT, StateT>;
public:
    using typename SockT::Protocol;
    using typename SockT::Endpoint;
    using PollHandle = toolbox::PollHandle;
    using State = StateT;
    using StateSignal = io::BasicSocketStateSignal<State>;
public:
    using Base::Base, Base::get;
    using Base::read, Base::write;

    template<typename ReactorT>
    explicit BasicSocket(ReactorT& r)
    : poll_(r.poll(get()))
    {
    }

    template<typename ReactorT>
    explicit BasicSocket(ReactorT& r, Protocol protocol)
    : Base(protocol)
    , poll_(r.poll(get()))
    {
        open_.prepare(*impl());
        io_slot(util::bind<&DerivedT::on_io_event>(impl()));
    }

     BasicSocket() = default;

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

    void state(SocketState val) {
        auto old = state_;
        state_ = val; 
        state_changed_(old, val, std::error_code{});
    }

    StateSignal state_changed() { return state_changed_; }

    Endpoint& local() { return local_; }

    void bind(const Endpoint& ep) {
        Base::bind(ep);
        local_ = ep;        
    }

    template<typename ReactorT>
    void open(ReactorT& r, Protocol protocol = {}) {
        *static_cast<SockT*>(this) = SockT(protocol);
        poll_ = r.poll(get());        
        open_.prepare(*impl());
        io_slot(util::bind<&DerivedT::on_io_event>(impl()));
    }
  
    void close() {
        poll_.reset();
        Base::close();        
    }

    /// returns false if we already reading
    bool can_read() const { return read_.empty(); }
    
    /// returns false if we already writing
    bool can_write() const { return write_.empty(); }

    void async_read(MutableBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        read_.flags(0);
        read_.prepare(*impl(), slot, buffer) ;
    }
    
    void async_recv(MutableBuffer buffer, int flags, Slot<ssize_t, std::error_code> slot) {
        read_.flags(flags);
        read_.prepare(*impl(), slot, buffer);
    }

    void async_write(ConstBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        read_.flags(0);
        write_.prepare(*impl(), slot,  buffer);
    }

    void async_zc_write(std::size_t size, Slot<void*, std::size_t> mut, Slot<ssize_t, std::error_code> slot) {
        read_.flags(0);
        write_.prepare(*impl(), slot, size, mut);
    }

    IoSlot io_slot() { return io_slot_; }  

    void io_slot(IoSlot slot) {
        if(io_slot_ != slot) {
            io_slot_ = slot;
            poll().mod(slot);        
        }
    }
    // for MultiSocket support
    constexpr std::size_t size() const { return 1; }

    DerivedT* impl() { return static_cast<DerivedT*>(this); }
protected:    
    void on_io_event(CyclTime now, os::FD fd, PollEvents events) {        
        auto old_batching = poll().batching(true); // disable commits of poll flags while in the cycle
        for(std::size_t i=0; i<64; i++) {
            // read something
            if(events & PollEvents::Read) {
                while(read_) {
                    if(!read_.complete(*impl(), events)) {
                        events = events - PollEvents::Read; // read will block
                        break;
                    }
                }
            }            
            // write ready things
            if(events & PollEvents::Write) {
                while(write_) {
                    if(!write_.complete(*impl(), events)) {
                        events = events - PollEvents::Write; // write will block
                        break;
                    }
                }
            }
            if(!(events & (PollEvents::Read+PollEvents::Write)))
                break; // nothing more to do
        }
        poll().batching(old_batching);
        poll().commit(true);    // always commit in the end of processing
    }
protected:
    IoSlot io_slot_ {};
    PollHandle poll_;
    SocketRead<DerivedT,Endpoint> read_; // could be specialized
    SocketWrite<DerivedT,Endpoint> write_;
    SockOpen<DerivedT> open_;
    Buffer buf_;
    State state_ {State::Closed};    
    StateSignal state_changed_;
    Endpoint local_;
};


/// socket with broadcast semantics
template<typename SocketT>
class MultiSocket {
    using This = MultiSocket<SocketT>;
public:
    using Endpoint = typename SocketT::Endpoint;
    using Protocol = typename SocketT::Protocol;
    using Socket = SocketT;

    Socket& emplace_back(Socket&& socket) { return sockets_.emplace_back(std::move(socket));} 
    Socket& back() { return sockets_.back();}
    Socket& operator[](std::size_t index) { return sockets_[index]; }
    std::size_t size() const { return sockets_.size(); }
    auto begin() { return sockets_.begin(); }
    auto end() { return sockets_.end(); }

    void async_zc_write(std::size_t size, Slot<void*, std::size_t> mut, Slot<ssize_t, std::error_code> slot) {
        if(sockets_.empty())
            return;
        assert(!write_);
        write_.set_slot(slot);
        done_ = 0;
        for(int i=0;i<sockets_.size();i++) {
            sockets_[i].async_zc_write(size, mut, util::bind<&This::on_write>(this));
        }
    }

    void async_write(ConstBuffer buf, Slot<ssize_t, std::error_code> slot) {
        if(sockets_.empty())
            return;
        assert(!write_);
        write_.set_slot(slot);
        done_ = 0;
        for(int i=0;i<sockets_.size();i++) {
            sockets_[i].async_write(buf, util::bind<&This::on_write>(this));
        }
    }
    void on_write(ssize_t size, std::error_code ec) {
        done_++;
        if(done_>=sockets_.size()) { // everybody ready
            write_.notify(size, ec);
        }
    }
private:
    std::vector<Socket> sockets_;
    SocketWrite<This, Endpoint> write_;
    int done_ {};
};

template<typename SocketT, typename EndpointT>
class SocketWrite<MultiSocket<SocketT>, EndpointT> : public CompletionSlot<ssize_t, std::error_code> {
public:
};

}}