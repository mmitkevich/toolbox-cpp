#pragma once

#include "toolbox/io/Socket.hpp"
#include <functional>

namespace toolbox { inline namespace io {


template<typename DerivedT, typename SockT, typename StateT>
class BasicDgramSocket : public BasicSocket< DerivedT, SockT, StateT>
{
    using This = BasicDgramSocket<DerivedT, SockT, StateT>;
    using Base = BasicSocket<DerivedT, SockT, StateT>;
    DerivedT* self() { return static_cast<DerivedT*>(this); } 
public:
    using typename Base::Endpoint;
    using typename Base::Protocol;
    using typename Base::State;
    using Base::Base;
    using Base::open;
    
    using ClientSocket = std::reference_wrapper<This>;

    void async_recvfrom(MutableBuffer buffer, int flags, Endpoint& endpoint, Slot<ssize_t, std::error_code> slot) {
        //static_assert(SocketTraits::has_recvfrom<SockT>);
        Base::read_.flags(flags);
        Base::read_.endpoint(&endpoint);
        Base::read_.prepare(*self(), slot, buffer);
    }

    void async_sendto(ConstBuffer buffer, const Endpoint& endpoint, Slot<ssize_t, std::error_code> slot) {
        assert(Base::write_.empty());
        Base::write_.endpoint(&endpoint);
        Base::write_.prepare(*self(), slot, buffer);
    }

    void async_zc_sendto(std::size_t size, Slot<void*, std::size_t> mut, int flags, const Endpoint& endpoint, Slot<ssize_t, std::error_code> slot) {
        Base::write_.flags(flags);
        Base::write_.endpoint(&endpoint);
        Base::write_.prepare(*self(), slot, size, mut);
    }
};

// SocketRead for recvfrom
template<typename DerivedT, typename SockT, typename StateT, typename EndpointT>
class SocketRead<BasicDgramSocket<DerivedT, SockT, StateT>, EndpointT>
: public SocketRead<BasicSocket<BasicDgramSocket<DerivedT, SockT, StateT>, SockT, StateT>, EndpointT>
{ 
    using This = BasicDgramSocket<DerivedT,SockT, StateT>;
    using Base = SocketRead<BasicSocket<This, SockT, StateT>, EndpointT>;
    using Socket = This;
    using Endpoint = EndpointT;
  public:
    using Base::empty, Base::notify;
    using typename Base::Slot;

    bool prepare(Socket& socket, Slot slot, MutableBuffer buf) {
        return Base::prepare(socket, slot, buf);
    }
    
    bool complete(Socket& socket, PollEvents events) {
        std::error_code ec{};
        assert(Base::endpoint_);
        ssize_t size = socket.recvfrom(Base::buf_, Base::flags_, *Base::endpoint_, ec);
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
template<typename DerivedT, typename SockT, typename StateT, typename EndpointT>
class SocketWrite<BasicDgramSocket<DerivedT, SockT, StateT>, EndpointT> 
: public SocketWrite<BasicSocket<BasicDgramSocket<DerivedT, SockT, StateT>, SockT, StateT>, EndpointT>
{ 
    using This = BasicDgramSocket<DerivedT, SockT, StateT>;
    using Base = SocketWrite<BasicSocket<This, SockT, StateT>, EndpointT>;
    using Socket = This;
    using Endpoint = EndpointT;
  public:
    using Base::prepare, Base::empty, Base::notify;
    using typename Base::Slot;
    
    bool prepare(Socket& socket, Slot slot, ConstBuffer buf) {
        return Base::prepare(socket, slot, buf);
    }

    bool complete(Socket& socket, PollEvents events) {
        std::error_code ec{};
        ssize_t size {};
        assert(Base::endpoint_);
        auto& ep = *Base::endpoint_;
        assert(!(ep==Endpoint{}));
        if(!Base::data_.data()) {
            auto wbuf = Base::buf_.prepare(Base::data_.size());
            Base::mut_(wbuf.data(),wbuf.size());
            size = socket.sendto(wbuf, Base::flags_, ep, ec);
        }else {
            size = socket.sendto(Base::data_, Base::flags_, ep, ec);
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

class DgramSocket : public BasicDgramSocket<DgramSocket, DgramSock, io::SocketState> {
    using Base = BasicDgramSocket<DgramSocket, DgramSock, io::SocketState>;  
  public:
    using Base::Base;
};

}} // toolbox::io