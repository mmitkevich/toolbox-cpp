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
inline namespace net {


template<typename SocketT>
class SocketConnect : public CompletionSlot<std::error_code> {
    using Base=CompletionSlot<std::error_code>;
  public:
    using Socket=SocketT;
    using Endpoint=typename Socket::Endpoint;
    using Base::Base, Base::empty, Base::notify, Base::operator bool, Base::set_slot, Base::reset;

    bool prepare(Socket& socket, const Endpoint& ep, Slot slot) {
        if(*this) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "connect" };
        }
        set_slot(slot);
        std::error_code ec {};
        socket.connect(ep, ec);
        if (ec.value() == EINPROGRESS) { //ec != std::errc::operation_in_progress
            socket.poll().add(PollEvents::Write);
            return false;
        }
        if (ec) {
            throw std::system_error{ec, "connect"};
        }
        return true;
    }
    bool complete(Socket& socket, PollEvents events) {
        std::error_code ec{};
        if((events & PollEvents::Error)) {
            ec = socket.get_error();
            socket.state(SocketState::Disconnected);
        } else if((events & PollEvents::Write)) {
            socket.state(SocketState::Connected);
        }
        socket.poll().del(PollEvents::Write);   
        notify(ec); // they could start read/write, etc
        return true; // all done
    }
};

template<typename SockT>
class BasicStreamSocket : public BasicSocket<SockT> {
    using This = BasicStreamSocket<SockT>;
    using Base = BasicSocket<SockT>;
public:
    using typename Base::PollHandle;
    using typename Base::Endpoint;
public:
    using Base::Base;

    using Base::state, Base::poll;
    using Base::read, Base::write, Base::recv;

    template<typename HandlerT>
    void async_connect(const Endpoint& ep, HandlerT&& handler) {
        async_connect(ep, util::bind<HandlerT>());
    }
    void async_connect(const Endpoint& ep, Slot<std::error_code> slot) {
        state(SocketState::Connecting);

        if(!conn_.prepare(*this, ep, slot)) {
            poll().mod(conn_slot());            // async
        } else {
            poll().mod(io_slot());              // sync
        }
        poll().commit();
    }
    
  protected:
    friend Base;
    using Base::io_slot, Base::on_io_event;
    void on_sock_prepare(Sock& sock) {
        Base::on_sock_prepare(sock);
        if (sock.is_ip_family()) {
            set_tcp_no_delay(sock.get(), true);
        }
    }
    IoSlot conn_slot() { return util::bind<&This::on_conn_event>(this); }
    void on_conn_event(CyclTime now, os::FD fd, PollEvents events) {
        if(conn_) {
            poll().mod(io_slot());   
            poll().commit();         
            conn_.complete(*this, events);
        }
    }
protected:
    SocketConnect<This> conn_;
};

using StreamSocket = BasicStreamSocket<StreamSockClnt>;

} // namespace net
} // namespace toolbox


