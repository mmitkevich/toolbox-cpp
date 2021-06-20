#pragma once

#include "toolbox/io/Socket.hpp"
#include "toolbox/util/String.hpp"
#include <functional>

namespace toolbox { inline namespace io {
template<typename Self, typename SockT>

class BasicDgramSocket : public BasicSocket< Self, SockT>
{
    using This = BasicDgramSocket<Self, SockT>;
    using Base = BasicSocket<Self, SockT>;
    Self* self() { return static_cast<Self*>(this); } 
public:
    using typename Base::Endpoint;
    using typename Base::Protocol;
    using typename Base::State;
    using Base::Base;
    using Base::open;
    
    using ClientSocket = std::reference_wrapper<This>;
    
    using typename Base::SocketOpen;
    class SocketRead: public Base::SocketRead { 
        using Base = typename BasicSocket<Self, SockT>::SocketRead;
    public:
        using Base::empty, Base::notify;
        using typename Base::Slot;
        bool prepare(Self& self, Slot slot, MutableBuffer buf) {
            return Base::prepare(self, slot, buf);
        }
        bool complete(Self& self, PollEvents events) {
            std::error_code ec{};
            assert(Base::endpoint_);
            ssize_t size = self.recvfrom(Base::buf_, Base::flags_, *Base::endpoint_, ec);
            TOOLBOX_DUMPV(6)<<"dgram recvfrom(flags="<<Base::flags_<<",size="<<size<<",remote="<<*Base::endpoint_<<",ec:"<<ec<<")";
            TOOLBOX_DUMPV(7)<<"dgram recvfrom:\n"<<util::to_hex_dump(std::string_view{(char*)Base::buf_.data(),(std::size_t)size});            
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
    };
    class SocketWrite: public Base::SocketWrite { 
        using Base = typename BasicSocket<Self, SockT>::SocketWrite;
    public:
        using Base::prepare, Base::empty, Base::notify;
        using typename Base::Slot;
        
        bool prepare(Self& self, Slot slot, ConstBuffer buf) {
            return Base::prepare(self, slot, buf);
        }

        bool complete(Self& self, PollEvents events) {
            std::error_code ec{};
            ssize_t size {};
            assert(Base::endpoint_);
            auto& ep = *Base::endpoint_;
            assert(!(ep==Endpoint{}));

            //TOOLBOX_DUMPV(5) << "dgram sendto "<<ep<<" size="<<Base::data_.size()<<tb::to_hex_dump(std::string_view{(const char*)Base::data_.data(), Base::data_.size()});
            if(!Base::data_.data()) {
                auto wbuf = Base::buf_.prepare(Base::data_.size());
                Base::mut_(wbuf.data(),wbuf.size());
                size = self.sendto(wbuf, Base::flags_, ep, ec);
            }else {
                size = self.sendto(Base::data_, Base::flags_, ep, ec);
            }
            TOOLBOX_DUMPV(6)<<"dgram sendto(flags="<<Base::flags_<<",size="<<size<<",remote="<<*Base::endpoint_<<",ec:"<<ec<<")";
            TOOLBOX_DUMPV(7)<<"dgram sendto:\n"<<util::to_hex_dump(std::string_view{(char*)Base::data_.data(),(std::size_t)size});            
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
    };

    void async_recvfrom(MutableBuffer buffer, int flags, Endpoint& endpoint, Slot<ssize_t, std::error_code> slot) {
        //static_assert(SocketTraits::has_recvfrom<SockT>);
        assert(!self()->read_impl());
        self()->read_impl().flags(flags);
        self()->read_impl().endpoint(&endpoint);
        self()->read_impl().prepare(*self(), slot, buffer);
    }
    void async_sendto(ConstBuffer buffer, const Endpoint& endpoint, Slot<ssize_t, std::error_code> slot) {
        assert(!self()->write_impl());
        self()->write_impl().endpoint(&endpoint);
        self()->write_impl().prepare(*self(), slot, buffer);
    }
    template<class T>
    void async_sendto(std::size_t size, Slot<T*, std::size_t> mut, int flags, const Endpoint& endpoint, Slot<ssize_t, std::error_code> slot) {
        assert(!self()->write_impl());
        self()->write_impl().flags(flags);
        self()->write_impl().endpoint(&endpoint);
        self()->write_impl().prepare(*self(), slot, size, mut);
    }
};

/// crtp final
template<class DgramSockT=net::DgramSock>
class DgramSocket : public BasicDgramSocket<DgramSocket<DgramSockT>, DgramSockT> {
    using Base = BasicDgramSocket<DgramSocket<DgramSockT>, DgramSockT>;
    using typename Base::SocketRead, typename Base::SocketWrite, typename Base::SocketOpen;
  public:
    using Base::Base;
    SocketOpen& open_impl() { return open_impl_; }
    SocketRead& read_impl() { return read_impl_; }
    SocketWrite& write_impl() { return write_impl_; }
  protected:
    SocketOpen open_impl_;
    SocketRead read_impl_;
    SocketWrite write_impl_;
};

}} // toolbox::io