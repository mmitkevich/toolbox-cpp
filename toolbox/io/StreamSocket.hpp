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

template<typename SockT, typename PollT, typename DerivedT>
class BasicStreamSocket : public BasicSocket<SockT, PollT, DerivedT> {
    using This = BasicStreamSocket<SockT, PollT, DerivedT>;
    using Base = BasicSocket<SockT, PollT, DerivedT>;
public:
    using Sock = SockT;
    using typename Base::PollHandle;
    using typename Base::Endpoint;
public:
    using Base::Base;

    using Base::state, Base::sock, Base::poll, Base::impl;
    using Base::read, Base::write, Base::recv;

    void connect(const Endpoint& ep, Slot<std::error_code> slot) {
        state(SocketState::Connecting);

        if(!conn_.empty()) {
            throw std::system_error{make_error_code(std::errc::operation_in_progress), "connect"};
        }
        poll().mod(PollEvents::Read + PollEvents::Write, conn_slot());
        std::error_code ec {};
        sock().connect(ep, ec);
        if (ec && ec.value() != EINPROGRESS) { //ec != std::errc::operation_in_progress
            throw std::system_error{ec, "connect"};
        }
        conn_ = {SockOpId::Connect, slot};
        if(!ec) {
            poll().mod(impl().io_slot());
            conn_.complete(ec);
        }
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
    IoSlot conn_slot() { return util::bind<&DerivedT::on_conn_event>(this); }
    void on_conn_event(CyclTime now, os::FD fd, PollEvents events) {
        //assert(state_==SocketState::Connecting);
        if(conn_.empty())
            return; // in case we got error but didn't close
        if((events & PollEvents::Error)) {
            std::error_code ec = sock().get_error();
            state(SocketState::Disconnected);
            conn_.complete(ec);
        } else if((events & PollEvents::Write)) {
            std::error_code ec {};
            state(SocketState::Connected);
            poll().mod(PollEvents::Read, io_slot());   
            conn_.complete(ec);
        }
    }
protected:
    SockOp conn_;
};

class StreamSocket  : public BasicStreamSocket<StreamSockClnt, PollHandle, StreamSocket> {
  using Base = BasicStreamSocket<StreamSockClnt, PollHandle, StreamSocket>;
public:
  using Sock = typename Base::Sock;
  using PollHandle = typename Base::PollHandle;
public:
  using Base::Base;
  using Base::state, Base::sock, Base::poll;
  using Base::read, Base::write, Base::recv;
};

} // namespace net
} // namespace toolbox


