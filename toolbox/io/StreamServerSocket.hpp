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
#include <toolbox/io/StreamSocket.hpp>

namespace toolbox {
inline namespace io {

template<typename SocketT, typename ClientSocketT>
class SocketAccept : public CompletionSlot<ClientSocketT&&, std::error_code> {
    using Base = CompletionSlot<std::error_code>;
  public:
    using Socket = SocketT;
    using ClientSocket = ClientSocketT;
    using Endpoint=typename Socket::Endpoint;
    using typename Base::SlotImpl;
    using Base::Base, Base::empty, Base::notify, Base::operator bool, Base::set_slot, Base::reset;

    bool prepare(Socket& socket, SlotImpl slot, Endpoint* ep) {
        if(*this) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "accept" };
        }
        endpoint_ = ep;
        set_slot(slot);
        socket.poll().add(PollEvents::Read);
        return true;
    }
    bool complete(Socket& socket, PollEvents events) {
        if(events & PollEvents::Read) {
            std::error_code ec {};
            auto sock = socket.accept(*endpoint_, ec);
            socket.poll().del(PollEvents::Read);
            ClientSocket client {socket};
            notify(std::move(client), ec);
            return true;
        }
        return false;
    }
protected:
    Endpoint *endpoint_{};
};

/// adds listen and accept
template<typename SockT, typename SockClntT, typename StateT>
class BasicStreamServerSocket : public BasicSocket<
    BasicStreamServerSocket<SockT, SockClntT, StateT>, 
    SockT, StateT>
{
    using This = BasicStreamServerSocket<SockT, SockClntT, StateT>;
    using Base = BasicSocket<This, SockT, StateT>;
public:
    using typename Base::PollHandle;
    using typename Base::Protocol;
    using typename Base::Endpoint;
    using ClientSocket = BasicStreamSocket<SockClntT, StateT>;
    using AcceptOp = SocketAccept<This, ClientSocket>;
public:
    using Base::Base;
    using Base::poll, Base::sock, Base::state;
    using Base::read, Base::write, Base::recv;
    
    PollHandle poll(SockClntT& sock) {
        PollHandle p = poll();
        p.fd(sock.get());
        return p;
    }

    void listen(int backlog=0) { 
        state(SocketState::Listening);
        sock().listen(backlog);
    }
    void accept(Endpoint& ep, Slot<ClientSocket&&, std::error_code> slot) {
        accept_.prepare(*this, slot, &ep);
    }
protected:
    void on_io_event(CyclTime now, os::FD fd, PollEvents events) {
        if(accept_) {
            accept_.complete(poll(), sock());
        }
    }  
protected:
    AcceptOp accept_;
};

template<typename SockT, typename SockClntT, typename StateT>
class SockOpen<BasicStreamServerSocket<SockT, SockClntT, StateT>> {
    using Socket = BasicStreamServerSocket<SockT, SockClntT, StateT>;
  public:
    void prepare(Socket& socket) {
        socket.set_non_block();
        if (socket.is_ip_family()) {
            set_tcp_no_delay(socket.get(), true);
        }
        socket.set_reuse_addr(true);
    }
};

using StreamServerSocket = BasicStreamServerSocket<net::StreamSockServ, net::StreamSockClnt, io::SocketState>;

} // namespace net
} // namespace toolbox


