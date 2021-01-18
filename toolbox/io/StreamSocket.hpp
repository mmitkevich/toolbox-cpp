// The Reactive C++ Toolbox.
// Copyright (C) 2013-2019 Swirly Cloud Limited
// Copyright (C) 2020 Reactive Markets Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "toolbox/io/Buffer.hpp"
#include "toolbox/io/PollHandle.hpp"
#include "toolbox/sys/Error.hpp"
#include <asm-generic/errno.h>
#include <exception>
#include <system_error>
#include <toolbox/io/Event.hpp>
#include <toolbox/io/Reactor.hpp>
#include <toolbox/net/StreamSock.hpp>
#include <toolbox/io/Socket.hpp>

namespace toolbox {
inline namespace io {


template<class Self, class SockT, typename StateT>
class BasicStreamSocket : public BasicSocket<Self, SockT, StateT> {
    using Base = BasicSocket<Self, SockT, StateT>;
    Self* self() { return static_cast<Self*>(this); }
public:
    using typename Base::PollHandle;
    using typename Base::Endpoint;
    using typename Base::Protocol;
    using typename Base::State;
public:
    using Base::Base;

    class SocketConnect : public CompletionSlot<std::error_code> {
        using Base = CompletionSlot<std::error_code>;
    public:
        using Base::Base, Base::empty, Base::invoke, Base::operator bool, Base::set_slot, Base::reset;
        using typename Base::Slot;

        bool prepare(Self& self, const Endpoint& ep, Slot slot) {
            if(*this) {
                throw std::system_error { make_error_code(std::errc::operation_in_progress), "connect" };
            }
            set_slot(slot);
            std::error_code ec {};
            self.remote() = ep;
            self.connect(ep, ec);
            if (ec.value() == EINPROGRESS) { //ec != std::errc::operation_in_progress
                self.poll().add(PollEvents::Write);
                return false;
            }
            if (ec) {
                throw std::system_error{ec, "connect"};
            }
            return true;
        }
        bool complete(Self& self, PollEvents events) {
            std::error_code ec{};
            if((events & PollEvents::Error)) {
                ec = self.get_error();
                self.remote() = {}; // didn't connect
                self.state(State::Closed);
            } else if((events & PollEvents::Write)) {
                self.state(State::Open);
            }
            self.poll().del(PollEvents::Write);   
            invoke(ec); // they could start read/write, etc
            return true; // all done
        }
    };

    using Base::state, Base::poll;
    using Base::read, Base::write;

    template<typename HandlerT>
    void async_connect(const Endpoint& ep, HandlerT&& handler) {
        async_connect(ep, util::bind<HandlerT>());
    }

    void async_connect(const Endpoint& ep, Slot<std::error_code> slot) {
        state(State::Connecting);
        if(!conn_.prepare(*this, ep, slot)) {
            poll().mod(conn_slot());            // async
        } else {
            poll().mod(io_slot());              // sync
        }
    }
    //Endpoint& remote() { return remote_; }
    //const Endpoint& remote() const { return remote_; }
  protected:
    friend Base;
    using Base::io_slot, Base::on_io_event;
    IoSlot conn_slot() { return util::bind<&Self::on_conn_event>(self()); }
    void on_conn_event(CyclTime now, os::FD fd, PollEvents events) {
        if(conn_) {
            poll().mod(io_slot());   
            conn_.complete(*this, events);
        }
    }
protected:
    SocketConnect conn_;
    //Endpoint remote_ {};   // what we connect to
};

template<class SockClntT, typename StateT>
class StreamSocket : public BasicStreamSocket<StreamSocket<SockClntT, StateT>, SockClntT, StateT> {
    using Base = BasicStreamSocket<StreamSocket<SockClntT, StateT>, SockClntT, StateT>;
    using typename Base::SocketConnect;
  public:
    using Base::Base;
  protected:
    friend Base;
    SocketConnect& connect_impl() { return connect_impl_; }
  protected:
    SocketConnect connect_impl_;
};


/// adds listen and accept
template<class Self, class SockT, class ClientSocketT, typename StateT>
class BasicStreamServerSocket : public BasicSocket<Self, SockT, StateT>
{
    using Base = BasicSocket<Self, SockT, StateT>;
    Self* self() { return static_cast<Self*>(this); }
    using typename Base::SocketOpen;
public:
    using typename Base::PollHandle;
    using typename Base::Protocol;
    using typename Base::Endpoint;
    using ClientSocket = ClientSocketT;
public:
    using Base::Base;
    using Base::poll, Base::state;
    using Base::read, Base::write, Base::recv;
    
    class SockOpen: public BasicSocket<Self, SockT, StateT>::SockOpen {
        using Base = typename BasicSocket<Self, SockT, StateT>::SockOpen;
      public:
        void prepare(Self& self) {
            Base::prepare(self);
            if (self.is_ip_family()) {
                set_tcp_no_delay(self.get(), true);
            }
            self.set_reuse_addr(true);
        }
    };

    class SocketAccept : public CompletionSlot<ClientSocket&&, std::error_code> {
        using Base = CompletionSlot<ClientSocket&&, std::error_code>;
    public:
        using Base::Base, Base::empty, Base::invoke, Base::operator bool, Base::set_slot, Base::reset;
        using typename Base::Slot;
        bool prepare(Self& self, Slot slot, Endpoint* ep) {
            if(*this) {
                throw std::system_error { make_error_code(std::errc::operation_in_progress), "accept" };
            }
            endpoint_ = ep;
            set_slot(slot);
            self.poll().add(PollEvents::Read);
            return true;
        }
        bool complete(Self& self, PollEvents events) {
            if(events & PollEvents::Read) {
                std::error_code ec {};
                auto sock = self.accept(*endpoint_, ec);
                self.poll().del(PollEvents::Read);
                ClientSocket client {self};
                invoke(std::move(client), ec);
                return true;
            }
            return false;
        }
    protected:
        Endpoint *endpoint_{};
    };

    PollHandle poll(ClientSocket& sock) {
        PollHandle p = poll();
        p.fd(sock.get());
        return p;
    }

    void listen(int backlog=0) { 
        state(SocketState::Listening);
        Base::listen(backlog);
    }
    void async_accept(Endpoint& ep, Slot<ClientSocket&&, std::error_code> slot) {
        self()->accept_impl().prepare(*self(), slot, &ep);
    }
protected:
    void on_io_event(CyclTime now, os::FD fd, PollEvents events) {
        if(self()->accept_impl()) {
            self()->accept_impl().complete(*self(), events);
        }
    }  
};

template<class SockServT=StreamSockServ, class SockClntT=StreamSockClnt, typename StateT=io::SocketState>
class StreamServerSocket : public  BasicStreamServerSocket<
    StreamServerSocket<SockServT, StreamSockClnt>, 
    SockServT, StreamSocket<SockClntT, StateT>, StateT> 
{
    using Base = BasicStreamServerSocket<
        StreamServerSocket<SockServT, StreamSockClnt>,
        SockServT, StreamSocket<SockClntT, StateT>, StateT>;
    using typename Base::SocketAccept, typename Base::SocketOpen;
  public:
    using Base::Base;
  protected:
    friend Base;
    friend typename Base::Base;
    SocketAccept& accept_impl() { return accept_impl_ ;}
    SocketOpen& open_impl() { return open_impl_; }
  protected:
    SocketAccept accept_impl_;
    SocketOpen open_impl_;
};

} // namespace io
} // namespace toolbox


