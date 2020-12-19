#pragma once

#include "toolbox/io/Socket.hpp"

namespace toolbox { inline namespace io {


template<typename SockT, typename StateT>
class BasicDgramSocket : public BasicSocket< BasicDgramSocket<SockT, StateT>, SockT, StateT>
{
    using This = BasicDgramSocket<SockT, StateT>;
    using Base = BasicSocket<This, SockT, StateT>;
public:
    using typename Base::Endpoint;
    using ClientSocket = This;

    using Base::Base;
    Endpoint& remote() { return remote_; }
    
    /// sets this.remote to dgram received
    void async_read(MutableBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        async_recvfrom(buffer, 0, remote_, slot);
    }

    void async_recvfrom(MutableBuffer buffer, int flags, Endpoint& endpoint, Slot<ssize_t, std::error_code> slot) {
        static_assert(SocketTraits::has_recvfrom<Base>);
        Base::read_.prepare(*this, slot, buffer, flags, &endpoint);
    }

    /// Write to this.remote
    void async_write(ConstBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        async_sendto(buffer, remote_, slot);
    }

    void async_sendto(ConstBuffer buffer, const Endpoint& endpoint, Slot<ssize_t, std::error_code> slot) {
        Base::write_.endpoint(&endpoint);
        Base::write_.prepare(*this, slot, buffer);
    }

    void async_zc_write(std::size_t size, Slot<void*, std::size_t> mut, Slot<ssize_t, std::error_code> slot) {
        async_zc_sendto(size, mut, remote_, slot);
    }

    void async_zc_sendto(std::size_t size, Slot<void*, std::size_t> mut, int flags, Endpoint& endpoint, Slot<ssize_t, std::error_code> slot) {
        Base::write_.flags(flags);
        Base::write_.endpoint(endpoint);
        Base::write_.prepare(*this, slot, size, mut);
    }
protected:
    Endpoint remote_;   // last remote
};

// SocketRead for recvfrom
template<typename SockT, typename StateT, typename EndpointT>
class SocketRead<BasicDgramSocket<SockT, StateT>, EndpointT>
: public SocketRead<BasicSocket<BasicDgramSocket<SockT, StateT>, SockT, StateT>, EndpointT>
{ 
    using This = BasicDgramSocket<SockT, StateT>;
    using Base = SocketRead<BasicSocket<This, SockT, StateT>, EndpointT>;
    using Socket = BasicDgramSocket<SockT, StateT>;
    using Endpoint = EndpointT;
  public:
    using Base::empty, Base::notify;

    bool complete(Socket& socket, PollEvents events) {
        std::error_code ec{};
        ssize_t size = socket.recvfrom(Base::buf_, Base::flags_, socket.remote(), ec);
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
};


// SocketWrite for sendto
template<typename SockT, typename StateT, typename EndpointT>
class SocketWrite<BasicDgramSocket<SockT, StateT>, EndpointT> 
: public SocketWrite<BasicSocket<BasicDgramSocket<SockT, StateT>, SockT, StateT>, EndpointT>
{ 
    using This = BasicDgramSocket<SockT, StateT>;
    using Base = SocketWrite<BasicSocket<This, SockT, StateT>, EndpointT>;
    using Socket = BasicDgramSocket<SockT, StateT>;
    using Endpoint = EndpointT;
  public:
    using Base::prepare, Base::empty, Base::notify;

    bool complete(Socket& socket, PollEvents events) {
        std::error_code ec{};
        ssize_t size {};
        if(!Base::data_.data()) {
            auto wbuf = Base::buf_.prepare(Base::data_.size());
            Base::mut_(wbuf.data(),wbuf.size());
            size = socket.sendto(wbuf, Base::flags_, socket.remote(), ec);
        }else {
            size = socket.sendto(Base::data_, Base::flags_, socket.remote(), ec);
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
};

using DgramSocket = BasicDgramSocket<DgramSock, io::SocketState>;



}} // toolbox::io