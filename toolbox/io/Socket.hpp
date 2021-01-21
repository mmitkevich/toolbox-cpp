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
class CompletionSlot : public util::Slot<ArgsT...> {
public:
    using Slot = util::Slot<ArgsT...>;

    using Slot::empty, Slot::operator bool, Slot::reset;
    
    /// fire once and reset
    void operator()(ArgsT... args) { 
        invoke(std::forward<ArgsT>(args)...);
    }

    /// fire once and reset
    void invoke(ArgsT...args) {
        auto s = *this;
        reset();
        s.Slot::invoke(std::forward<ArgsT>(args)...);
    }

    void set_slot(Slot val) { 
        assert(!val.empty());
        *static_cast<Slot*>(this) = val;
    }

    Slot get_slot() const { return *this; }
};

template<typename...ArgsT>
class PendingSlot : public CompletionSlot<ArgsT...> {
    using Base = CompletionSlot<ArgsT...>;
public:
    void pending(int val) { pending_ = val; }
    int pending() const { return pending_; }
    void inc_pending() { pending_++; }
    void operator()(ArgsT...args) {
        if(--pending_==0)
            Base::operator()(std::forward<ArgsT>(args)...);
    }
protected:
    int pending_ {};
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


enum SocketFlags {
    ZeroCopy = 1
};



template<typename StateT>
class BasicSocketState {
public:
    using State = StateT;
    using StateSignal = Signal<State, State, std::error_code>;

    State state() const { return state_; }

    void state(State val) {
        auto old = state_;
        state_ = val; 
        state_changed_(old, val, std::error_code{});
    }
    StateSignal state_changed() { return state_changed_; }
protected:
    State state_ {};    
    StateSignal state_changed_ {};
};


/// Self should implement read_impl/write_impl/open_impl
template<typename Self, typename SockT, typename StateT>
class BasicSocket : public SockT, public BasicSocketState<StateT> {
    using Base = SockT;
    using SocketState = BasicSocketState<StateT>;
    using This = BasicSocket<Self, SockT, StateT>;
    Self* self() { return static_cast<Self*>(this); }
    const Self* self() const { return static_cast<const Self*>(this); }
public:
    using PollHandle = toolbox::PollHandle;
    using typename Base::Protocol;
    using typename Base::Endpoint;
    using typename SocketState::State;
public:
    using Base::Base, Base::get;
    using Base::read, Base::write;
    
    /// operations
    class SocketOpen {
    public:
        void prepare(Self& self) {
            self.set_non_block();
        }
    };

    class SocketRead : public CompletionSlot<ssize_t, std::error_code> {
        using Base = CompletionSlot<ssize_t, std::error_code>;
      public:
        using typename Base::Slot;
        using Base::Base, Base::empty, Base::invoke, Base::operator bool, Base::set_slot, Base::get_slot;

        void flags(int val) { flags_ = val; }
        void endpoint(Endpoint* ep) { endpoint_ = ep; }
        bool prepare(Self& self, Slot slot, MutableBuffer buf) {
            if(*this) {
                throw std::system_error { make_error_code(std::errc::operation_in_progress), "read" };
            }
            buf_ = buf;
            set_slot(slot);
            self.poll().add(PollEvents::Read);
            self.poll().commit();
            return false;   // async
        }
        void notify(ssize_t size, std::error_code ec) { 
            auto s = get_slot();
            reset();
            s.invoke(size, ec);
        }
        bool complete(Self& self, PollEvents events) {
            std::error_code ec{};
            ssize_t size;
            size = self.recv(buf_, flags_, ec);
            if(size<0 && ec.value()==EWOULDBLOCK) {
                self.poll().add(PollEvents::Read);
                return false; // no more
            } else {
                notify(size, ec);   // this will launch handlers they could make not-empty again

                if(empty()) {
                    self.poll().del(PollEvents::Read); // no write interest 
                }
                return true; // done
            }
        }
        void reset() { 
            Base::reset();
            //buf_ = {};  // keep last buf
            flags_ = 0;
            //endpoint_ = {}; // keep last ep
        }
        MutableBuffer& buf() { return buf_; }
      protected:
        MutableBuffer buf_ {};
        int flags_ {};    
        Endpoint* endpoint_ {};
    };

    class SocketWrite : public CompletionSlot<ssize_t, std::error_code> {
        using Base = CompletionSlot<ssize_t, std::error_code>;
    public:
        using typename Base::Slot;

        using Base::Base, Base::empty, Base::invoke, Base::operator bool, Base::set_slot;

        void flags(int val) { flags_ = val; }
        void endpoint(const Endpoint* ep) { endpoint_ = ep; }

        bool prepare(Self& self, Slot slot, ConstBuffer data) {
            if(*this) {
                throw std::system_error { make_error_code(std::errc::operation_in_progress), "write" };
            }
            data_ = data;
            set_slot(slot);
            self.poll().add(PollEvents::Write);
            return false; // async
        }
        // zero copy
        bool prepare(Self& self, Slot slot, std::size_t size, util::Slot<void*, std::size_t> mut) {
            if(*this) {
                throw std::system_error { make_error_code(std::errc::operation_in_progress), "write" };
            }
            data_ = {nullptr, size}; // zero copy
            set_mut(mut);
            set_slot(slot);
            self.poll().add(PollEvents::Write);
            return false;
        }
        void set_mut(util::Slot<void*, std::size_t> mut) {
            mut_ = mut;
        }
        void notify(ssize_t size, std::error_code ec) { 
            // redefine to use This::reset
            auto s = *static_cast<Base*>(this);
            reset();
            s.invoke(size, ec);
        }
        bool complete(Self& self, PollEvents events) {
            std::error_code ec {};
            ssize_t size;
            if(!data_.data()) {
                auto wbuf = buf_.prepare(data_.size());
                mut_(wbuf.data(), wbuf.size());
                size = self.write(wbuf, ec);
            }else {
                size = self.write(data_, ec);
            }

            if(size<0 && ec.value()==EWOULDBLOCK) {
                self.poll().add(PollEvents::Write);
                return false; // no more
            } else {
                notify(size, ec); // this will launch handlers they could make not-empty again
                if(empty()) {
                    self.poll().del(PollEvents::Write);
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

    BasicSocket()  = default;

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
        self()->open_impl().prepare(*self());
        io_slot(util::bind<&Self::on_io_event>(self()));
    }

    ~BasicSocket() {
        close();
    }
    
     // Copy.
    BasicSocket(const BasicSocket&) = delete;
    BasicSocket& operator=(const BasicSocket&) = delete;

    // Move.
    BasicSocket(BasicSocket&&)  = default;
    BasicSocket& operator=(BasicSocket&&)  = default;    

    PollHandle& poll() { return poll_; }

    template<typename ReactorT>
    void open(ReactorT& r, Protocol protocol = {}) {
        *static_cast<SockT*>(this) = SockT(protocol);
        poll_ = r.poll(get());        
        self()->open_impl().prepare(*self());
        io_slot(util::bind<&Self::on_io_event>(self()));
    }    
  
    void close() {
        poll_.reset();
        Base::close();        
    }
    
    /// returns False if we already reading
    bool can_read()  { return self()->read_impl().empty(); }
    
    /// returns false if we already writing
    bool can_write()  { return self()->write_impl().empty(); }

    void async_read(MutableBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        self()->read_impl().flags(0);
        self()->read_impl().prepare(*self(), slot, buffer) ;
    }
    
    void async_recv(MutableBuffer buffer, int flags, Slot<ssize_t, std::error_code> slot) {
        self()->read_impl().flags(flags);
        self()->read_impl().prepare(*self(), slot, buffer);
    }

    MutableBuffer rbuf() {
        return self()->read_impl().buf();
    }

    void async_write(ConstBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        self()->write_impl().flags(0);
        self()->write_impl().prepare(*self(), slot,  buffer);
    }

    /// zero copy write
    template<class T>
    void async_write(std::size_t size, Slot<T*, std::size_t> mut, Slot<ssize_t, std::error_code> slot) {
        self()->write_impl().flags(0);
        self()->write_impl().prepare(*self(), slot, size, mut);
    }

    ConstBuffer wbuf() {
        return self()->write_impl().buf_;
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
protected:    
    void on_io_event(CyclTime now, os::FD fd, PollEvents events) {        
        auto old_batching = poll().batching(true); // disable commits of poll flags while in the cycle
        for(std::size_t i=0; i<64; i++) {
            bool again = false;
            // read something
            if(events & PollEvents::Read) {
                while(self()->read_impl()) {
                    if(!self()->read_impl().complete(*self(), events)) {
                        events = events - PollEvents::Read; // read will block
                        break;
                    }
                    again = true;   // some interest
                }
            }            
            // write ready things
            if(events & PollEvents::Write) {
                while(self()->write_impl()) {
                    if(!self()->write_impl().complete(*self(), events)) {
                        events = events - PollEvents::Write; // write will block
                        break;
                    }
                    again = true;   // some interest
                }
            }
            if(!again || !(events & (PollEvents::Read+PollEvents::Write)))
                break; // nothing more to do
        }
        poll().batching(old_batching);
        poll().commit(true);    // always commit in the end of processing
    }
protected:
    IoSlot io_slot_ {};
    //SocketRead<DerivedT,Endpoint> read_; // could be specialized
    //SocketWrite<DerivedT,Endpoint> write_;
    //SockOpen<DerivedT> open_;
    Buffer buf_;
    PollHandle poll_;    
    Endpoint local_;
    Endpoint remote_;
};

}}