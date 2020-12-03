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
#include "toolbox/io/ReactorHandle.hpp"
#include "toolbox/sys/Error.hpp"
#include <asm-generic/errno.h>
#include <exception>
#include <system_error>
#include <toolbox/io/Event.hpp>
#include <toolbox/io/Reactor.hpp>
#include <toolbox/net/StreamSock.hpp>
#include <toolbox/io/Socket.hpp>
#include <toolbox/io/StreamSocket.hpp>

namespace toolbox {
inline namespace io {

template<typename SockT, typename ClientSocketT>
class SockAcceptOp : public BasicSockOp<ClientSocketT&&, std::error_code> {
    using Base = BasicSockOp<ClientSocketT&&, std::error_code>;
    public:
    using Base::Base;
    using Slot = typename Base::Slot;
    using Sock = SockT;
    using Endpoint = typename Sock::Endpoint;
    using ClientSocket = ClientSocketT;


    SockAcceptOp(SockOpId id, Slot slot, Endpoint&& ep)
    : Base(id, slot)
    , endpoint_(ep) {}
    Endpoint& endpoint() { return endpoint_;}
    
    void complete(Reactor& reactor, Sock& serv) {
        std::error_code ec {};
        auto sock = serv.accept(endpoint_, ec);
        sock.set_non_block();
        if (sock.is_ip_family()) {
            set_tcp_no_delay(sock.get(), true);
        }
        ClientSocket client {reactor, std::move(sock) };
        complete(std::move(client), ec);
    }
    protected:
    Endpoint endpoint_;
};

template<typename SockT, typename ClientSocketT, typename DerivedT>
class BasicServerSocket : public BasicSocket<SockT, DerivedT> {
    using This = BasicServerSocket<SockT, ClientSocketT, DerivedT>;
    using Base = BasicSocket<SockT, DerivedT>;
public:
    using Sock = SockT;
    using ClientSocket = ClientSocketT;
    using Protocol = typename Sock::Protocol;
    using Endpoint = typename Sock::Endpoint;
    using AcceptOp = SockAcceptOp<Sock, ClientSocket>;
    using AcceptSlot = typename AcceptOp::Slot;
public:
    using Base::Base;
    using Base::reactor, Base::sock, Base::state;
    using Base::read, Base::write, Base::recv;
    
    void listen(int backlog=0) { 
        state(SocketState::Listening);
        sock().listen(backlog);
    }
    void accept(AcceptSlot slot) {
        assert(state()==SocketState::Listening);
        if(!accept_.empty()) {
            throw std::system_error{make_error_code(std::errc::operation_in_progress), "accept"};
        }
        accept_ = {SockOpId::Accept, slot};
    }
protected:
    constexpr PollEvents poll_events() { return PollEvents::Read; }  
    void on_sock_open(Sock& sock) {
        Base::on_sock_open(sock);
        sock.set_reuse_addr(true);
        if (sock.is_ip_family()) {
            sock.set_tcp_no_delay(true);
        }
        Base::sub_ = reactor().subscribe(sock.get(), poll_events(), 
            util::bind<&This::on_io_event>(this));
    }
    void on_io_event(CyclTime now, os::FD fd, PollEvents events) {
        //assert(state_==SocketState::Listening);
        if(!accept_.empty()) {
            accept_.complete(reactor(), sock());
        }
    }  
protected:
    AcceptOp accept_;
};

class ServerSocket : public BasicServerSocket<StreamSockServ, StreamSocket, ServerSocket> {
    using Base = BasicServerSocket<StreamSockServ, StreamSocket, ServerSocket>;
public:
    using Sock = typename Base::Sock;
public:
    using Base::Base;
    using Base::reactor, Base::sock;
    using Base::read, Base::write, Base::recv;
    using Base::accept, Base::listen, Base::bind;
};

} // namespace net
} // namespace toolbox


